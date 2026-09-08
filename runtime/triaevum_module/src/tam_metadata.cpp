#include "triaevum/tam_metadata.h"

#include "triaevum/module_abi.h"
#include "triaevum/service_abi.h"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace triaevum::module {
namespace {

constexpr std::size_t kMaximumJsonDepth = 32U;
constexpr std::size_t kMaximumJsonNodes = 4096U;
constexpr std::size_t kMaximumPhysicalMemoryRegions = 64U;

enum class JsonKind { Null, Boolean, Number, String, Object, Array };

struct JsonMember;

struct JsonValue {
  JsonKind kind = JsonKind::Null;
  bool boolean = false;
  std::string text;
  std::vector<JsonMember> members;
  std::vector<JsonValue> elements;

  [[nodiscard]] const JsonValue *Find(std::string_view name) const;
};

// std::vector supports incomplete elements; std::pair's traits need complete types.
struct JsonMember {
  std::string first;
  JsonValue second;
};

const JsonValue *JsonValue::Find(std::string_view name) const {
  const auto found = std::find_if(
      members.begin(), members.end(),
      [name](const auto &member) { return member.first == name; });
  return found == members.end() ? nullptr : &found->second;
}

bool IsValidUtf8(std::string_view value) {
  const auto *bytes = reinterpret_cast<const std::uint8_t *>(value.data());
  std::size_t index = 0U;
  while (index < value.size()) {
    const std::uint8_t first = bytes[index++];
    if (first <= 0x7FU) {
      continue;
    }
    std::uint32_t codePoint = 0U;
    std::size_t trailing = 0U;
    std::uint32_t minimum = 0U;
    if (first >= 0xC2U && first <= 0xDFU) {
      codePoint = first & 0x1FU;
      trailing = 1U;
      minimum = 0x80U;
    } else if (first >= 0xE0U && first <= 0xEFU) {
      codePoint = first & 0x0FU;
      trailing = 2U;
      minimum = 0x800U;
    } else if (first >= 0xF0U && first <= 0xF4U) {
      codePoint = first & 0x07U;
      trailing = 3U;
      minimum = 0x10000U;
    } else {
      return false;
    }
    if (trailing > value.size() - index) {
      return false;
    }
    for (std::size_t count = 0U; count < trailing; ++count) {
      const std::uint8_t next = bytes[index++];
      if ((next & 0xC0U) != 0x80U) {
        return false;
      }
      codePoint = (codePoint << 6U) | (next & 0x3FU);
    }
    if (codePoint < minimum || codePoint > 0x10FFFFU ||
        (codePoint >= 0xD800U && codePoint <= 0xDFFFU)) {
      return false;
    }
  }
  return true;
}

void AppendUtf8(std::uint32_t codePoint, std::string *destination) {
  if (codePoint <= 0x7FU) {
    destination->push_back(static_cast<char>(codePoint));
  } else if (codePoint <= 0x7FFU) {
    destination->push_back(static_cast<char>(0xC0U | (codePoint >> 6U)));
    destination->push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
  } else if (codePoint <= 0xFFFFU) {
    destination->push_back(static_cast<char>(0xE0U | (codePoint >> 12U)));
    destination->push_back(
        static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU)));
    destination->push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
  } else {
    destination->push_back(static_cast<char>(0xF0U | (codePoint >> 18U)));
    destination->push_back(
        static_cast<char>(0x80U | ((codePoint >> 12U) & 0x3FU)));
    destination->push_back(
        static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU)));
    destination->push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
  }
}

class JsonParser {
public:
  explicit JsonParser(std::span<const std::uint8_t> input)
      : mInput(reinterpret_cast<const char *>(input.data()), input.size()) {}

  bool Parse(JsonValue *result, std::string *error) {
    if (result == nullptr || error == nullptr) {
      return false;
    }
    SkipWhitespace();
    if (!ParseValue(0U, result)) {
      *error = mError;
      return false;
    }
    SkipWhitespace();
    if (mPosition != mInput.size()) {
      Fail("JSON has trailing non-whitespace bytes");
      *error = mError;
      return false;
    }
    return true;
  }

private:
  bool ParseValue(std::size_t depth, JsonValue *result) {
    if (depth > kMaximumJsonDepth || ++mNodes > kMaximumJsonNodes) {
      return Fail("JSON complexity limit exceeded");
    }
    SkipWhitespace();
    if (mPosition >= mInput.size()) {
      return Fail("JSON value is truncated");
    }
    const char next = mInput[mPosition];
    if (next == '{') {
      return ParseObject(depth, result);
    }
    if (next == '[') {
      return ParseArray(depth, result);
    }
    if (next == '"') {
      result->kind = JsonKind::String;
      return ParseString(&result->text);
    }
    if (next == 't') {
      result->kind = JsonKind::Boolean;
      result->boolean = true;
      return ParseLiteral("true");
    }
    if (next == 'f') {
      result->kind = JsonKind::Boolean;
      result->boolean = false;
      return ParseLiteral("false");
    }
    if (next == 'n') {
      result->kind = JsonKind::Null;
      return ParseLiteral("null");
    }
    result->kind = JsonKind::Number;
    return ParseNumber(&result->text);
  }

  bool ParseObject(std::size_t depth, JsonValue *result) {
    result->kind = JsonKind::Object;
    ++mPosition;
    SkipWhitespace();
    if (Consume('}')) {
      return true;
    }
    while (mPosition < mInput.size()) {
      std::string name;
      if (!ParseString(&name)) {
        return false;
      }
      if (std::any_of(
              result->members.begin(), result->members.end(),
              [&name](const auto &member) { return member.first == name; })) {
        return Fail("JSON object contains a duplicate key");
      }
      SkipWhitespace();
      if (!Consume(':')) {
        return Fail("JSON object key has no value separator");
      }
      JsonValue value;
      if (!ParseValue(depth + 1U, &value)) {
        return false;
      }
      result->members.emplace_back(std::move(name), std::move(value));
      SkipWhitespace();
      if (Consume('}')) {
        return true;
      }
      if (!Consume(',')) {
        return Fail("JSON object member separator is invalid");
      }
      SkipWhitespace();
    }
    return Fail("JSON object is truncated");
  }

  bool ParseArray(std::size_t depth, JsonValue *result) {
    result->kind = JsonKind::Array;
    ++mPosition;
    SkipWhitespace();
    if (Consume(']')) {
      return true;
    }
    while (mPosition < mInput.size()) {
      JsonValue value;
      if (!ParseValue(depth + 1U, &value)) {
        return false;
      }
      result->elements.push_back(std::move(value));
      SkipWhitespace();
      if (Consume(']')) {
        return true;
      }
      if (!Consume(',')) {
        return Fail("JSON array element separator is invalid");
      }
      SkipWhitespace();
    }
    return Fail("JSON array is truncated");
  }

  bool ParseString(std::string *result) {
    if (!Consume('"')) {
      return Fail("JSON object key or string is invalid");
    }
    result->clear();
    while (mPosition < mInput.size()) {
      const auto current = static_cast<std::uint8_t>(mInput[mPosition++]);
      if (current == '"') {
        return IsValidUtf8(*result) || Fail("JSON string is not valid UTF-8");
      }
      if (current < 0x20U) {
        return Fail("JSON string contains an unescaped control byte");
      }
      if (current != '\\') {
        result->push_back(static_cast<char>(current));
        continue;
      }
      if (mPosition >= mInput.size()) {
        return Fail("JSON string escape is truncated");
      }
      const char escaped = mInput[mPosition++];
      switch (escaped) {
      case '"':
      case '\\':
      case '/':
        result->push_back(escaped);
        break;
      case 'b':
        result->push_back('\b');
        break;
      case 'f':
        result->push_back('\f');
        break;
      case 'n':
        result->push_back('\n');
        break;
      case 'r':
        result->push_back('\r');
        break;
      case 't':
        result->push_back('\t');
        break;
      case 'u': {
        std::uint32_t codePoint = 0U;
        if (!ParseHex4(&codePoint)) {
          return false;
        }
        if (codePoint >= 0xD800U && codePoint <= 0xDBFFU) {
          if (mPosition + 2U > mInput.size() ||
              mInput.substr(mPosition, 2U) != "\\u") {
            return Fail("JSON high surrogate has no low surrogate");
          }
          mPosition += 2U;
          std::uint32_t low = 0U;
          if (!ParseHex4(&low) || low < 0xDC00U || low > 0xDFFFU) {
            return Fail("JSON low surrogate is invalid");
          }
          codePoint =
              0x10000U + ((codePoint - 0xD800U) << 10U) + (low - 0xDC00U);
        } else if (codePoint >= 0xDC00U && codePoint <= 0xDFFFU) {
          return Fail("JSON contains an unpaired low surrogate");
        }
        AppendUtf8(codePoint, result);
        break;
      }
      default:
        return Fail("JSON string escape is invalid");
      }
    }
    return Fail("JSON string is truncated");
  }

  bool ParseHex4(std::uint32_t *value) {
    if (mPosition + 4U > mInput.size()) {
      return Fail("JSON Unicode escape is truncated");
    }
    *value = 0U;
    for (std::size_t index = 0U; index < 4U; ++index) {
      const char digit = mInput[mPosition++];
      std::uint32_t nibble = 0U;
      if (digit >= '0' && digit <= '9') {
        nibble = static_cast<std::uint32_t>(digit - '0');
      } else if (digit >= 'a' && digit <= 'f') {
        nibble = static_cast<std::uint32_t>(digit - 'a') + 10U;
      } else if (digit >= 'A' && digit <= 'F') {
        nibble = static_cast<std::uint32_t>(digit - 'A') + 10U;
      } else {
        return Fail("JSON Unicode escape contains a non-hex digit");
      }
      *value = (*value << 4U) | nibble;
    }
    return true;
  }

  bool ParseLiteral(std::string_view literal) {
    if (mInput.substr(mPosition, literal.size()) != literal) {
      return Fail("JSON literal is invalid");
    }
    mPosition += literal.size();
    return true;
  }

  bool ParseNumber(std::string *result) {
    const std::size_t begin = mPosition;
    if (Consume('-') && mPosition >= mInput.size()) {
      return Fail("JSON number is truncated");
    }
    if (Consume('0')) {
      if (mPosition < mInput.size() && mInput[mPosition] >= '0' &&
          mInput[mPosition] <= '9') {
        return Fail("JSON number has a leading zero");
      }
    } else if (ConsumeDigits() == 0U) {
      return Fail("JSON number has no integer digits");
    }
    if (Consume('.') && ConsumeDigits() == 0U) {
      return Fail("JSON number has no fractional digits");
    }
    if (mPosition < mInput.size() &&
        (mInput[mPosition] == 'e' || mInput[mPosition] == 'E')) {
      ++mPosition;
      if (mPosition < mInput.size() &&
          (mInput[mPosition] == '+' || mInput[mPosition] == '-')) {
        ++mPosition;
      }
      if (ConsumeDigits() == 0U) {
        return Fail("JSON number has no exponent digits");
      }
    }
    *result = std::string(mInput.substr(begin, mPosition - begin));
    return true;
  }

  std::size_t ConsumeDigits() {
    const std::size_t begin = mPosition;
    while (mPosition < mInput.size() && mInput[mPosition] >= '0' &&
           mInput[mPosition] <= '9') {
      ++mPosition;
    }
    return mPosition - begin;
  }

  bool Consume(char expected) {
    if (mPosition < mInput.size() && mInput[mPosition] == expected) {
      ++mPosition;
      return true;
    }
    return false;
  }

  void SkipWhitespace() {
    while (mPosition < mInput.size() &&
           (mInput[mPosition] == ' ' || mInput[mPosition] == '\t' ||
            mInput[mPosition] == '\r' || mInput[mPosition] == '\n')) {
      ++mPosition;
    }
  }

  bool Fail(std::string message) {
    if (mError.empty()) {
      mError = std::move(message) + " at byte " + std::to_string(mPosition);
    }
    return false;
  }

  std::string_view mInput;
  std::size_t mPosition = 0U;
  std::size_t mNodes = 0U;
  std::string mError;
};

TamMetadataParseResult Failure(TamMetadataErrorCode code, std::string message) {
  TamMetadataParseResult result;
  result.error = {code, std::move(message)};
  return result;
}

const JsonValue *Required(const JsonValue &object, std::string_view name,
                          JsonKind kind, TamMetadataParseResult *failure) {
  const JsonValue *value = object.Find(name);
  if (value == nullptr) {
    *failure = Failure(TamMetadataErrorCode::MissingField,
                       "TAM metadata is missing field: " + std::string(name));
    return nullptr;
  }
  if (value->kind != kind) {
    *failure =
        Failure(TamMetadataErrorCode::InvalidField,
                "TAM metadata field has wrong type: " + std::string(name));
    return nullptr;
  }
  return value;
}

bool ReadUnsigned(const JsonValue &value, std::uint64_t *result) {
  if (value.text.empty() || value.text.front() == '-' ||
      value.text.find_first_of(".eE") != std::string::npos) {
    return false;
  }
  const char *begin = value.text.data();
  const char *end = begin + value.text.size();
  const auto conversion = std::from_chars(begin, end, *result);
  return conversion.ec == std::errc{} && conversion.ptr == end;
}

bool ReadHash(std::string_view text, std::array<std::uint8_t, 32> *result) {
  if (text.size() != result->size() * 2U) {
    return false;
  }
  const auto nibble = [](char value, std::uint8_t *output) {
    if (value >= '0' && value <= '9') {
      *output = static_cast<std::uint8_t>(value - '0');
      return true;
    }
    if (value >= 'a' && value <= 'f') {
      *output = static_cast<std::uint8_t>(value - 'a' + 10);
      return true;
    }
    return false;
  };
  for (std::size_t index = 0U; index < result->size(); ++index) {
    std::uint8_t high = 0U;
    std::uint8_t low = 0U;
    if (!nibble(text[index * 2U], &high) ||
        !nibble(text[index * 2U + 1U], &low)) {
      return false;
    }
    (*result)[index] = static_cast<std::uint8_t>((high << 4U) | low);
  }
  return true;
}

bool RangesOverlap(std::uint32_t leftBase, std::uint32_t leftSize,
                   std::uint32_t rightBase, std::uint32_t rightSize) {
  const std::uint64_t leftEnd = static_cast<std::uint64_t>(leftBase) + leftSize;
  const std::uint64_t rightEnd =
      static_cast<std::uint64_t>(rightBase) + rightSize;
  return static_cast<std::uint64_t>(leftBase) < rightEnd &&
         static_cast<std::uint64_t>(rightBase) < leftEnd;
}

} // namespace

TamMetadataParseResult
ParseTamMetadataV1(std::span<const std::uint8_t> jsonBytes) {
  JsonValue root;
  std::string jsonError;
  if (!JsonParser(jsonBytes).Parse(&root, &jsonError) ||
      root.kind != JsonKind::Object) {
    return Failure(TamMetadataErrorCode::InvalidJson,
                   jsonError.empty() ? "TAM metadata root is not an object"
                                     : std::move(jsonError));
  }

  TamMetadataParseResult result;
  const JsonValue *format = Required(root, "format", JsonKind::String, &result);
  const JsonValue *privateArtifact =
      Required(root, "private_local_artifact", JsonKind::Boolean, &result);
  const JsonValue *redistributable =
      Required(root, "redistributable", JsonKind::Boolean, &result);
  const JsonValue *runtimeAbi =
      Required(root, "runtime_abi", JsonKind::Number, &result);
  const JsonValue *querySymbol =
      Required(root, "query_symbol", JsonKind::String, &result);
  const JsonValue *recipe = Required(root, "recipe", JsonKind::String, &result);
  const JsonValue *targetTriple =
      Required(root, "target_triple", JsonKind::String, &result);
  const JsonValue *sourceIdentity =
      Required(root, "source_identity_sha256", JsonKind::String, &result);
  const JsonValue *translatorIdentity =
      Required(root, "translator_identity_sha256", JsonKind::String, &result);
  const JsonValue *nativeImage =
      Required(root, "native_image", JsonKind::Object, &result);
  const JsonValue *titleAotImage = root.Find("title_aot_image");
  const JsonValue *forgeVersion =
      Required(root, "forge_tool_version", JsonKind::String, &result);
  const JsonValue *requiredServices =
      Required(root, "required_services", JsonKind::Array, &result);
  const JsonValue *physicalMemoryRegions = root.Find("physical_memory_regions");
  if (!result.Ok()) {
    return result;
  }
  if (physicalMemoryRegions != nullptr &&
      physicalMemoryRegions->kind != JsonKind::Array) {
    return Failure(TamMetadataErrorCode::InvalidField,
                   "TAM physical memory regions are not an array");
  }
  if (titleAotImage != nullptr && titleAotImage->kind != JsonKind::Object) {
    return Failure(TamMetadataErrorCode::InvalidField,
                   "TAM title AOT image is not an object");
  }
  const JsonValue *nativeBytes =
      Required(*nativeImage, "bytes", JsonKind::Number, &result);
  const JsonValue *nativeHash =
      Required(*nativeImage, "sha256", JsonKind::String, &result);
  if (!result.Ok()) {
    return result;
  }

  if (titleAotImage != nullptr) {
    const JsonValue *titleAbi =
        Required(*titleAotImage, "abi", JsonKind::Number, &result);
    const JsonValue *titleBytes =
        Required(*titleAotImage, "bytes", JsonKind::Number, &result);
    const JsonValue *titleHash =
        Required(*titleAotImage, "sha256", JsonKind::String, &result);
    if (!result.Ok()) {
      return result;
    }
    std::uint64_t parsedTitleAbi = 0U;
    if (!ReadUnsigned(*titleAbi, &parsedTitleAbi) || parsedTitleAbi != 1U ||
        !ReadUnsigned(*titleBytes, &result.metadata.titleAotImageBytes) ||
        result.metadata.titleAotImageBytes == 0U ||
        !ReadHash(titleHash->text, &result.metadata.titleAotImageSha256)) {
      return Failure(TamMetadataErrorCode::InvalidField,
                     "TAM title AOT metadata contract is invalid");
    }
    result.metadata.hasTitleAotImage = true;
    result.metadata.titleAotAbi = static_cast<std::uint32_t>(parsedTitleAbi);
  }

  std::uint64_t parsedRuntimeAbi = 0U;
  if (format->text != "triaevum_module_metadata_v1" ||
      !privateArtifact->boolean || redistributable->boolean ||
      !ReadUnsigned(*runtimeAbi, &parsedRuntimeAbi) ||
      parsedRuntimeAbi > std::numeric_limits<std::uint32_t>::max() ||
      querySymbol->text != TRIAEVUM_MODULE_QUERY_SYMBOL_V1 ||
      recipe->text.empty() || targetTriple->text.empty() ||
      forgeVersion->text.empty() ||
      !ReadHash(sourceIdentity->text, &result.metadata.sourceIdentitySha256) ||
      !ReadHash(translatorIdentity->text,
                &result.metadata.translatorIdentitySha256) ||
      !ReadUnsigned(*nativeBytes, &result.metadata.nativeImageBytes) ||
      result.metadata.nativeImageBytes == 0U ||
      !ReadHash(nativeHash->text, &result.metadata.nativeImageSha256)) {
    return Failure(TamMetadataErrorCode::InvalidField,
                   "TAM metadata v1 contract is invalid");
  }

  result.metadata.runtimeAbi = static_cast<std::uint32_t>(parsedRuntimeAbi);
  result.metadata.querySymbol = querySymbol->text;
  result.metadata.recipe = recipe->text;
  result.metadata.targetTriple = targetTriple->text;
  result.metadata.forgeToolVersion = forgeVersion->text;
  for (const JsonValue &service : requiredServices->elements) {
    if (service.kind != JsonKind::Object) {
      return Failure(TamMetadataErrorCode::InvalidField,
                     "TAM required service is not an object");
    }
    const JsonValue *id = Required(service, "id", JsonKind::Number, &result);
    const JsonValue *schema =
        Required(service, "schema_version", JsonKind::Number, &result);
    if (!result.Ok()) {
      return result;
    }
    std::uint64_t parsedId = 0U;
    std::uint64_t parsedSchema = 0U;
    if (!ReadUnsigned(*id, &parsedId) ||
        !ReadUnsigned(*schema, &parsedSchema) || parsedId == 0U ||
        parsedId > std::numeric_limits<std::uint32_t>::max() ||
        parsedSchema != TRIAEVUM_SERVICE_SCHEMA_V1 ||
        std::any_of(result.metadata.requiredServices.begin(),
                    result.metadata.requiredServices.end(),
                    [parsedId](const auto &existing) {
                      return existing.id == parsedId;
                    })) {
      return Failure(TamMetadataErrorCode::InvalidField,
                     "TAM required service contract is invalid");
    }
    result.metadata.requiredServices.push_back(
        {static_cast<std::uint32_t>(parsedId),
         static_cast<std::uint32_t>(parsedSchema)});
  }

  if (physicalMemoryRegions != nullptr) {
    if (physicalMemoryRegions->elements.size() >
        kMaximumPhysicalMemoryRegions) {
      return Failure(TamMetadataErrorCode::InvalidField,
                     "TAM declares too many physical memory regions");
    }
    for (const JsonValue &region : physicalMemoryRegions->elements) {
      if (region.kind != JsonKind::Object) {
        return Failure(TamMetadataErrorCode::InvalidField,
                       "TAM physical memory region is not an object");
      }
      const JsonValue *physicalBase =
          Required(region, "physical_base", JsonKind::Number, &result);
      const JsonValue *guestBase =
          Required(region, "guest_base", JsonKind::Number, &result);
      const JsonValue *bytes =
          Required(region, "bytes", JsonKind::Number, &result);
      if (!result.Ok()) {
        return result;
      }
      std::uint64_t parsedPhysicalBase = 0U;
      std::uint64_t parsedGuestBase = 0U;
      std::uint64_t parsedBytes = 0U;
      constexpr std::uint64_t addressSpaceBytes =
          static_cast<std::uint64_t>(
              std::numeric_limits<std::uint32_t>::max()) +
          1U;
      if (!ReadUnsigned(*physicalBase, &parsedPhysicalBase) ||
          !ReadUnsigned(*guestBase, &parsedGuestBase) ||
          !ReadUnsigned(*bytes, &parsedBytes) || parsedBytes == 0U ||
          parsedPhysicalBase > std::numeric_limits<std::uint32_t>::max() ||
          parsedGuestBase > std::numeric_limits<std::uint32_t>::max() ||
          parsedBytes > std::numeric_limits<std::uint32_t>::max() ||
          parsedPhysicalBase + parsedBytes > addressSpaceBytes ||
          parsedGuestBase + parsedBytes > addressSpaceBytes) {
        return Failure(TamMetadataErrorCode::InvalidField,
                       "TAM physical memory region is out of range");
      }
      const TamModuleMetadataV1::PhysicalMemoryRegion candidate = {
          static_cast<std::uint32_t>(parsedPhysicalBase),
          static_cast<std::uint32_t>(parsedGuestBase),
          static_cast<std::uint32_t>(parsedBytes)};
      if (std::any_of(
              result.metadata.physicalMemoryRegions.begin(),
              result.metadata.physicalMemoryRegions.end(),
              [&candidate](const auto &existing) {
                return RangesOverlap(candidate.physicalBase,
                                     candidate.byteCount, existing.physicalBase,
                                     existing.byteCount) ||
                       RangesOverlap(candidate.guestBase, candidate.byteCount,
                                     existing.guestBase, existing.byteCount);
              })) {
        return Failure(TamMetadataErrorCode::InvalidField,
                       "TAM physical memory regions overlap");
      }
      result.metadata.physicalMemoryRegions.push_back(candidate);
    }
  }
  const bool requiresPica = std::any_of(
      result.metadata.requiredServices.begin(),
      result.metadata.requiredServices.end(), [](const auto &service) {
        return service.id == TRIAEVUM_SERVICE_PICA_V1;
      });
  if (requiresPica && result.metadata.physicalMemoryRegions.empty()) {
    return Failure(TamMetadataErrorCode::MissingField,
                   "TAM PICA service requires physical memory regions");
  }
  return result;
}

bool TamTargetMatchesCurrentProcess(std::string_view target) {
#if defined(_WIN32)
#if defined(_M_ARM64) || defined(__aarch64__)
  return target == "aarch64-pc-windows-msvc";
#else
  return target == "x86_64-pc-windows-msvc";
#endif
#elif defined(__APPLE__)
#if defined(__aarch64__)
  return target == "aarch64-apple-darwin";
#else
  return target == "x86_64-apple-darwin";
#endif
#else
#if defined(__aarch64__)
  return target == "aarch64-unknown-linux-gnu";
#else
  return target == "x86_64-unknown-linux-gnu";
#endif
#endif
}

} // namespace triaevum::module
