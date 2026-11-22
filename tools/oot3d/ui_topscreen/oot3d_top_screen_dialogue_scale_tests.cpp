// TriAevum's dialogue size and dialogue font settings: the configuration
// fields (written only when changed), and the message window's element
// matrices scaled about the dialogue for its draw and restored after it, with
// guest memory left exactly as the native code wrote it.
#include "oot3d_native_a32_memory.h"
#include "oot3d_top_screen_config.h"
#include "oot3d_top_screen_dialogue_scale.h"

#include <bit>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace Oot3dNativeGame;

void Require(bool value, const char *message) {
  if (!value) {
    std::cerr << "dialogue scale: " << message << '\n';
    std::exit(1);
  }
}

bool Near(float a, float b) { return std::abs(a - b) < 1e-3F; }

void TestConfig() {
  TopScreenUiConfig config;
  Require(config.DialogueScalePercent == 100U && config.DialogueFont == TopScreenDialogueFont::Automatic,
          "the defaults are the game's size and the automatic font");
  std::string text, error;
  Require(SerializeTopScreenUiConfigText(config, &text, &error), "serialize the defaults");
  Require(text.find("dialogue_scale_percent") == std::string::npos && text.find("dialogue_font") == std::string::npos,
          "the defaults are not written (older builds reject unknown keys)");

  config.DialogueScalePercent = 70U;
  config.DialogueFont = TopScreenDialogueFont::N64;
  Require(SerializeTopScreenUiConfigText(config, &text, &error), "serialize a changed dialogue");
  Require(text.find("\"dialogue_scale_percent\": 70") != std::string::npos &&
              text.find("\"dialogue_font\": \"n64\"") != std::string::npos,
          "a changed dialogue is written");
  TopScreenUiConfig parsed;
  Require(ParseTopScreenUiConfigText(text, &parsed, &error) && parsed == config, "a changed dialogue round-trips");

  const auto rejects = [&](const char *fields) {
    TopScreenUiConfig ignored;
    return !ParseTopScreenUiConfigText(std::string("{\"schema\":\"oot3d_topscreen_ui_v2\",") + fields + "}", &ignored,
                                       &error);
  };
  Require(rejects("\"dialogue_scale_percent\":72"), "a size between the 5 percent steps");
  Require(rejects("\"dialogue_scale_percent\":45"), "a size below 50 percent");
  Require(rejects("\"dialogue_scale_percent\":105"), "a size above the game's");
  Require(rejects("\"dialogue_scale_percent\":0.7"), "a fractional size");
  Require(rejects("\"dialogue_font\":\"comic\""), "an unknown font");
  Require(rejects("\"dialogue_font\":1"), "a font that is not a name");
  for (const auto font : {TopScreenDialogueFont::Automatic, TopScreenDialogueFont::Original, TopScreenDialogueFont::N64}) {
    TopScreenDialogueFont named{};
    Require(ParseTopScreenDialogueFont(TopScreenDialogueFontName(font), &named) && named == font, "font names");
  }
}

constexpr std::uint32_t kContext = 0x005C1878U;
constexpr std::uint32_t kWindow = kContext + kTopScreenDialogueWindowOffset;
constexpr std::uint32_t kHeap = 0x08000000U;

struct Fixture {
  NativeA32Memory Memory;

  std::uint32_t Renderer(std::uint32_t index) const { return kHeap + 0x20000U + index * 0x100U; }

  void Matrix(std::uint32_t renderer, float scale, float x, float y) {
    const std::vector<float> values{scale, 0, 0, x, 0, scale, 0, y, 0, 0, 1, -0.01F};
    std::vector<std::uint8_t> bytes(values.size() * 4U);
    std::memcpy(bytes.data(), values.data(), bytes.size());
    Require(Memory.WriteBytes(renderer + 0x0CU, bytes), "seed a matrix");
  }

  // An element's renderer, and its second renderer (the text's shadow) with
  // whether the element draws it (+0x6E).
  void Element(std::uint32_t index, std::uint32_t renderer, bool visible, std::uint32_t second = 0U,
               bool secondDrawn = false) {
    const std::uint32_t element = kHeap + index * 0xD0U;
    Require(Memory.Write32(kWindow + kTopScreenDialogueElements + index * 4U, element) &&
                Memory.Write32(element + 0x64U, renderer) && Memory.Write8(element + 0x6DU, visible ? 1U : 0U) &&
                Memory.Write32(element + 0x68U, second) && Memory.Write8(element + 0x6EU, secondDrawn ? 1U : 0U),
            "seed an element");
  }

  float Word(std::uint32_t renderer, std::uint32_t word) {
    std::uint32_t bits = 0U;
    Require(Memory.Read32(renderer + 0x0CU + word * 4U, &bits), "read a matrix word");
    return std::bit_cast<float>(bits);
  }

  std::vector<std::uint8_t> Snapshot() {
    std::vector<std::uint8_t> heap(0x40000U), context(0x4000U), pause(0x10000U);
    Require(Memory.ReadBytes(kHeap, heap) && Memory.ReadBytes(0x005C0000U, context) &&
                Memory.ReadBytes(0x00500000U, pause),
            "snapshot");
    heap.insert(heap.end(), context.begin(), context.end());
    heap.insert(heap.end(), pause.begin(), pause.end());
    return heap;
  }
};

void TestScale() {
  Fixture f;
  std::string error;
  Require(f.Memory.MapRegion({"dialogue-pause", 0x00500000U, 0x10000U, true, false, {}}, &error) &&
              f.Memory.MapRegion({"dialogue-context", 0x005C0000U, 0x4000U, true, false, {}}, &error) &&
              f.Memory.MapRegion({"dialogue-heap", kHeap, 0x40000U, true, false, {}}, &error),
          "map the message window");
  Require(f.Memory.Write8(kWindow + 4U, 1U), "open the window");
  // The box, its text, the "next" triangle (sharing the box's renderer is a
  // renderer reached twice), and a hidden element far above the box.
  f.Matrix(f.Renderer(0), 1.0F, 200.0F, 190.0F);
  f.Matrix(f.Renderer(1), 1.0F, 124.0F, 171.0F);
  f.Matrix(f.Renderer(2), 1.25F, 300.0F, 205.0F);
  f.Matrix(f.Renderer(3), 1.0F, 50.0F, 20.0F);
  // The text's shadow, offset from it, and a second renderer not drawn.
  f.Matrix(f.Renderer(4), 1.0F, 125.5F, 172.5F);
  f.Matrix(f.Renderer(5), 1.0F, 10.0F, 10.0F);
  f.Element(0U, f.Renderer(0), true, f.Renderer(5), false);
  f.Element(1U, f.Renderer(1), true, f.Renderer(4), true);
  f.Element(2U, f.Renderer(2), true);
  f.Element(3U, f.Renderer(0), true);
  f.Element(9U, f.Renderer(3), false);
  const auto before = f.Snapshot();

  // The pivot: the screen's centre and the middle of the visible elements.
  const auto transform = ResolveTopScreenDialogueTransform(f.Memory, kWindow, 70U);
  Require(transform.Active && Near(transform.Scale, 0.7F) && Near(transform.PivotX, 200.0F) &&
              Near(transform.PivotY, (171.0F + 205.0F) * 0.5F),
          "the dialogue's pivot");
  Require(f.Snapshot() == before, "resolving the transform changed guest memory");

  TopScreenDialogueScaleState state;
  Require(BeginTopScreenDialogueScale(f.Memory, kWindow, 70U, state), "scale the window");
  Require(state.Restore.size() == 5U, "each renderer once, the hidden element's and the shadow's too");
  Require(Near(f.Word(f.Renderer(5), 0), 1.0F) && Near(f.Word(f.Renderer(5), 3), 10.0F),
          "a second renderer the element does not draw is untouched");
  const float pivotY = transform.PivotY;
  Require(Near(f.Word(f.Renderer(1), 0), 0.7F) && Near(f.Word(f.Renderer(1), 5), 0.7F) &&
              Near(f.Word(f.Renderer(1), 3), 200.0F + (124.0F - 200.0F) * 0.7F) &&
              Near(f.Word(f.Renderer(1), 7), pivotY + (171.0F - pivotY) * 0.7F),
          "the text scaled about the pivot");
  Require(Near(f.Word(f.Renderer(4), 0), 0.7F) && Near(f.Word(f.Renderer(4), 3), 200.0F + (125.5F - 200.0F) * 0.7F) &&
              Near(f.Word(f.Renderer(4), 7), pivotY + (172.5F - pivotY) * 0.7F),
          "the text's shadow scaled with it");
  Require(Near(f.Word(f.Renderer(2), 0), 1.25F * 0.7F) &&
              Near(f.Word(f.Renderer(2), 3), 200.0F + (300.0F - 200.0F) * 0.7F),
          "a larger element keeps its own size, scaled");
  Require(Near(f.Word(f.Renderer(0), 0), 0.7F) && Near(f.Word(f.Renderer(0), 3), 200.0F),
          "a shared renderer is scaled once");
  Require(Near(f.Word(f.Renderer(1), 10), 1.0F) && Near(f.Word(f.Renderer(1), 11), -0.01F),
          "the draw order (Z) is unchanged");
  EndTopScreenDialogueScale(f.Memory, state);
  Require(state.Restore.empty() && f.Snapshot() == before, "the draw's return restores the native matrices");

  // A draw whose return was not reached is restored by the next draw first.
  Require(BeginTopScreenDialogueScale(f.Memory, kWindow, 70U, state) &&
              BeginTopScreenDialogueScale(f.Memory, kWindow, 70U, state),
          "two draws without a return");
  Require(Near(f.Word(f.Renderer(1), 0), 0.7F), "a second draw does not scale twice");
  EndTopScreenDialogueScale(f.Memory, state);
  Require(f.Snapshot() == before, "restored after a missed return");

  // The game's own size, a closed window and an open pause page leave the
  // native draw alone.
  Require(!BeginTopScreenDialogueScale(f.Memory, kWindow, 100U, state) && f.Snapshot() == before, "100 percent");
  Require(f.Memory.Write32(0x0050672CU, 2U), "open the Items page");
  const auto paused = f.Snapshot();
  Require(!BeginTopScreenDialogueScale(f.Memory, kWindow, 70U, state) && f.Snapshot() == paused,
          "the item description under a pause page");
  Require(f.Memory.Write32(0x0050672CU, 0U) && f.Memory.Write8(kWindow + 4U, 0U), "close the page and the window");
  const auto closed = f.Snapshot();
  Require(!BeginTopScreenDialogueScale(f.Memory, kWindow, 70U, state) && f.Snapshot() == closed, "a closed window");

  // Unmapped memory: nothing is written, never a guess.
  NativeA32Memory empty;
  TopScreenDialogueScaleState none;
  Require(!BeginTopScreenDialogueScale(empty, kWindow, 70U, none) && none.Restore.empty(), "unmapped memory");
}

} // namespace

void RunTopScreenDialogueScaleTests() {
  TestConfig();
  TestScale();
}
