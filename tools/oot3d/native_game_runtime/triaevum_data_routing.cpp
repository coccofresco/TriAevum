#include "triaevum_data_routing.h"

#include <array>
#include <stdexcept>

namespace Oot3dNativeGame {
namespace {

// A routed folder: absolute and normal, from UTF-8. An empty value would
// silently mean "the working directory", so it is refused.
std::filesystem::path RoutedDirectory(std::string_view option, const char *value) {
    const std::string text(value);
    if (text.empty())
        throw std::runtime_error(std::string(option) + " requires a folder");
    const auto path = std::filesystem::u8path(text);
#if defined(__SWITCH__)
    return path.lexically_normal();
#else
    return std::filesystem::absolute(path).lexically_normal();
#endif
}

} // namespace

std::optional<uint64_t> ParseTitleId(std::string_view text) {
    if (text.size() != 16U)
        return std::nullopt;
    uint64_t value = 0;
    for (const char character : text) {
        uint64_t digit = 0;
        if (character >= '0' && character <= '9')
            digit = static_cast<uint64_t>(character - '0');
        else if (character >= 'a' && character <= 'f')
            digit = static_cast<uint64_t>(character - 'a' + 10);
        else if (character >= 'A' && character <= 'F')
            digit = static_cast<uint64_t>(character - 'A' + 10);
        else
            return std::nullopt;
        value = (value << 4U) | digit;
    }
    return value;
}

std::string FormatTitleId(uint64_t titleId) {
    constexpr std::array<char, 16> kDigits{'0', '1', '2', '3', '4', '5', '6', '7',
                                           '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
    std::string text(16U, '0');
    for (size_t index = 0; index < text.size(); ++index)
        text[text.size() - 1U - index] = kDigits[(titleId >> (index * 4U)) & 0xFU];
    return text;
}

bool ParseDataRoutingOption(int argc, char **argv, int &index, DataRouting &routing) {
    const std::string_view option(argv[index]);
    std::filesystem::path DataRouting::*folder = nullptr;
    if (option == "--log-directory")
        folder = &DataRouting::LogDirectory;
    else if (option == "--crash-directory")
        folder = &DataRouting::CrashDirectory;
    else if (option == "--texture-load-directory")
        folder = &DataRouting::TextureLoadDirectory;
    else if (option == "--texture-dump-directory")
        folder = &DataRouting::TextureDumpDirectory;
    else if (option == "--savestate-directory")
        folder = &DataRouting::SavestateDirectory;
    else if (option != "--title-id")
        return false;
    if (index + 1 >= argc)
        throw std::runtime_error(std::string(option) + " requires a value");
    const char *value = argv[index + 1];
    if (folder != nullptr) {
        routing.*folder = RoutedDirectory(option, value);
    } else {
        const auto title = ParseTitleId(value);
        if (!title.has_value())
            throw std::runtime_error("--title-id expects 16 hexadecimal digits: " + std::string(value));
        routing.TitleId = title;
    }
    index += 1;
    return true;
}

} // namespace Oot3dNativeGame
