#pragma once
#if defined(TRIAEVUM_DEVELOPER_TOOLS)
#include "oot3d_dev_free_camera.h"
#include "oot3d_dev_warp.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace Oot3dNativeGame {
// These are integration proof gates, not user preferences. Set each only after
// the corresponding DEVWARP ABI live receipt and mutation smoke is reviewed.
struct Mm3dDevProof {
  bool SceneTransition = false;
  bool FreeCamera = false;
  bool WarpPointRespawn = false;
  // Arrival options written before the transition (Common day, time, form).
  bool Day = false;
  bool TimeOfDay = false;
  bool PlayerForm = false;
};
// 2026-11-06 receipts (developer launch from saves/states/sct_day1_idle, Authentic,
// TRIAEVUM_DEV_WARP lines and RGB8 captures): time 22:00 in North Clock Town
// (night flag, night sky, HUD 10:02 PM); day 2 noon in East Clock Town (day 2
// rain, no dawn card); Deku form at Deku Palace (Deku HUD actions); Fierce Deity
// in Odolwa's Lair; a boot warp point at East Clock Town spawn 1's place with
// respawnFlag -8 arrives through spawn 0's entrance exactly at the point
// (742,100,240, yaw 0x4000, room 0), health 320 unchanged 90 frames later.
inline constexpr Mm3dDevProof kMm3dDevProof{true, true, true, true, true, true};

namespace Mm3dDevAbi {
inline constexpr uint32_t Common = 0x7751D8, ViewSlot = 0x656B5C, EntranceTable = 0x69FB44;
inline constexpr uint32_t CameraUpdate = 0x180B68;
inline constexpr std::array<std::pair<uint32_t, uint32_t>, 4> CameraGuard{{{CameraUpdate, 0xE92D4FF0},
                                                                           {CameraUpdate + 4, 0xE2815C01},
                                                                           {CameraUpdate + 8, 0xE1A04001},
                                                                           {CameraUpdate + 12, 0xE1A0A000}}};

template <class T> bool Read(DevWarpGuest &g, uint32_t a, T &v) {
  static_assert(std::endian::native == std::endian::little);
  return g.Read(a, std::span<uint8_t>(reinterpret_cast<uint8_t *>(&v), sizeof(v)));
}
struct Write {
  uint32_t Address;
  std::vector<uint8_t> Before, After;
};
class Transaction {
  DevWarpGuest &mGuest;
  std::vector<Write> mWrites;

public:
  explicit Transaction(DevWarpGuest &g) : mGuest(g) {}
  template <class T> bool Add(uint32_t address, const T &value) {
    Write w{address, std::vector<uint8_t>(sizeof(T)), std::vector<uint8_t>(sizeof(T))};
    if (!mGuest.Read(address, w.Before))
      return false;
    std::memcpy(w.After.data(), &value, sizeof(T));
    mWrites.push_back(std::move(w));
    return true;
  }
  bool Commit() {
    for (size_t i = 0; i < mWrites.size(); ++i) {
      if (mGuest.Write(mWrites[i].Address, mWrites[i].After))
        continue;
      // Include the attempted write: a guest writer may fail after partial IO.
      bool restored = true;
      for (size_t j = i + 1; j != 0; --j)
        restored = mGuest.Write(mWrites[j - 1].Address, mWrites[j - 1].Before) && restored;
      if (!restored)
        throw std::runtime_error("MM3D dev write rollback failed; stop guest execution");
      return false;
    }
    return true;
  }
};
inline bool Pointer(uint32_t p, uint32_t bytes) {
  return p >= 0x08000000 && !(p & 3U) && p < 0x40000000 && bytes <= 0x40000000 - p;
}
inline bool CodeMatches(DevWarpGuest &g) {
  uint32_t w = 0;
  for (const auto &[pc, expected] : CameraGuard)
    if (!Read(g, pc, w) || w != expected)
      return false;
  return Read(g, 0x15F4FC, w) && w == 0xE92D4FF0 && Read(g, 0x176AFC, w) && w == 0xE92D4FF0 && Read(g, 0x18E5BC, w) &&
         w == EntranceTable;
}
// Save fields of Common (Project Restoration's SaveData at Common+8; MM3D
// Play_Init 0x12A7AC reads time, isNight, nextDayTime the N64 way).
inline constexpr uint32_t EquippedMask = Common + 0x08;  // u8, 0 = none
inline constexpr uint32_t IsNight = Common + 0x0C;       // bool (Play_Init stores a word)
inline constexpr uint32_t Day = Common + 0x14;           // s32 1..3 (4 = after the final day)
inline constexpr uint32_t Time = Common + 0x20;          // u16, 0x0000 midnight, 0x8000 noon
inline constexpr uint32_t Form = Common + 0x26;          // u8: 0 Fierce Deity, 1 Goron, 2 Zora, 3 Deku, 4 Human
inline constexpr uint32_t NextDayTime = Common + 0x36A0; // u16, 0xFFFF none
inline constexpr uint32_t Health = Common + 0x13C;       // u16, 16 per heart (Restoration PlayerData)
enum : int32_t { FormDeity = 0, FormGoron = 1, FormZora = 2, FormDeku = 3, FormHuman = 4 };
// MM3D Play_Init: night when (time - 06:00) mod 24 h >= 12 h, i.e. before 06:00
// or from 18:00 (N64's morning boundary is 06:30).
inline bool NightAt(uint16_t time) { return uint16_t(time - 0x4000U) >= 0x8000U; }
// The scenes whose rules allow Fierce Deity: the boss lairs (2S2H scene ids).
inline bool DeityAllowed(uint8_t scene) {
  switch (scene) {
  case 0x0B: // Majora's Lair
  case 0x1F: // Odolwa's Lair
  case 0x36: // Twinmold's Lair
  case 0x44: // Goht's Lair
  case 0x5F: // Gyorg's Lair
    return true;
  default:
    return false;
  }
}
// The clock value of HH:MM; 06:00 is staged one past the morning boundary, as
// 2S2H's map select does (the generator's clock()).
inline uint16_t Clock(int hours, int minutes) {
  const int value = ((hours * 60 + minutes) * 0x10000 + 720) / 1440;
  return uint16_t(hours == 6 && minutes == 0 ? value + 1 : value);
}
inline std::string ClockText(uint16_t time) {
  const unsigned minutes = (unsigned(time) * 1440U) >> 16;
  char text[8];
  std::snprintf(text, sizeof(text), "%02u:%02u", minutes / 60U, minutes % 60U);
  return text;
}
inline const char *FormName(int32_t form) {
  static constexpr const char *kNames[]{"Fierce Deity", "Goron", "Zora", "Deku", "Human"};
  return form >= 0 && form <= 4 ? kNames[form] : "?";
}
inline uint32_t Play(DevWarpGuest &g) {
  uint32_t v = 0, owner = 0, right = 0, bottom = 0;
  uint8_t type = 0;
  if (!Read(g, ViewSlot, v) || v < 0x1F8 || !Pointer(v - 0x1F8, 0x11030))
    return 0;
  const uint32_t p = v - 0x1F8;
  if (!Read(g, p + 0x13D, type) || type != 4 || !Read(g, p + 0x4A8, owner) || owner != p || !Read(g, v + 8, right) ||
      right != 400 || !Read(g, v + 16, bottom) || bottom != 240)
    return 0;
  return p;
}
// Layer zero only. Bit-field validity is not table bounds validity.
inline bool Resolve(DevWarpGuest &g, uint16_t entrance, uint32_t &value) {
  const uint32_t group = entrance >> 9, spawn = (entrance >> 4) & 31;
  if (group >= 112 || (entrance & 15))
    return false;
  uint32_t count = 0, list = 0, row = 0, raw = 0;
  if (!Read(g, EntranceTable + 12 * group, count) || count > 32 || spawn >= count ||
      !Read(g, EntranceTable + 12 * group + 4, list) || list < 0x69E000 || list > EntranceTable || (list & 3U) ||
      4 * count > EntranceTable - list || !Read(g, list + 4 * spawn, row) || row < 0x69E000 ||
      row > EntranceTable - 4 || (row & 3U) || !Read(g, row, raw))
    return false;
  const auto signedScene = static_cast<int8_t>(raw & 255);
  const uint32_t scene = static_cast<uint32_t>(std::abs(static_cast<int>(signedScene)));
  if (scene > 0x74)
    return false;
  value = scene | (raw & 0xFF00);
  return true;
}
inline bool Finite(const std::array<float, 3> &v) {
  for (float x : v)
    if (!std::isfinite(x) || std::abs(x) > 1e7F)
      return false;
  return true;
}
} // namespace Mm3dDevAbi

#include "mm3d_dev_warp_catalog.inc"

// The arrival axes, in selection order.
enum Mm3dDevAxis : size_t { kMm3dAxisDay = 0, kMm3dAxisTime = 1, kMm3dAxisForm = 2 };
inline constexpr DevWarpChoice kMm3dDayChoices[]{{"Day 1", 1}, {"Day 2", 2}, {"Day 3", 3}};
inline constexpr DevWarpChoice kMm3dTimeChoices[]{
    {"06:00", 0x4001}, {"09:00", 0x6000}, {"12:00", 0x8000}, {"15:00", 0xA000}, {"18:00", 0xC000},
    {"20:00", 0xD555}, {"22:00", 0xEAAB}, {"00:00", 0x0000}, {"03:00", 0x2000}, {"05:00", 0x3555}};
inline constexpr DevWarpChoice kMm3dFormChoices[]{
    {"Human", Mm3dDevAbi::FormHuman}, {"Deku", Mm3dDevAbi::FormDeku}, {"Goron", Mm3dDevAbi::FormGoron},
    {"Zora", Mm3dDevAbi::FormZora}, {"Fierce Deity", Mm3dDevAbi::FormDeity}};

// TRIAEVUM_DEV_WARP for MM3D (agents, developer launch):
//   mm3d:<encoded entrance>[:day=<1-3>][:time=<HHMM>][:form=<human|deku|goron|zora|deity>]
//   mm3d:zone=<id>[:day=..][:time=..][:form=..]
// An entrance keeps every option it does not name; a zone takes its preset.
struct Mm3dDevWarpScript {
  std::optional<uint16_t> Entrance;
  std::string Zone;
  DevWarpSelection Arrival = kDevWarpKeepAll;
};
inline Mm3dDevWarpScript ParseMm3dDevWarpScript(std::string_view spec, std::span<const DevWarpEntrance> entrances,
                                                std::span<const DevWarpZone> zones) {
  const auto bad = [](const std::string &what) -> std::runtime_error {
    return std::runtime_error("TRIAEVUM_DEV_WARP: " + what +
                              " (mm3d:<entrance>|zone=<id>[:day=<1-3>][:time=<HHMM>][:form=<name>])");
  };
  if (!spec.starts_with("mm3d:"))
    throw bad("MM3D warps start with mm3d:");
  std::vector<std::string_view> fields;
  for (std::string_view rest = spec.substr(5);;) {
    const auto colon = rest.find(':');
    fields.push_back(rest.substr(0, colon));
    if (colon == std::string_view::npos)
      break;
    rest = rest.substr(colon + 1);
  }
  Mm3dDevWarpScript script;
  const std::string head(fields.front());
  if (head.starts_with("zone=")) {
    script.Zone = head.substr(5);
    const auto found = std::find_if(zones.begin(), zones.end(), [&](const auto &z) { return script.Zone == z.Id; });
    if (found == zones.end())
      throw bad("unknown zone '" + script.Zone + "'");
    script.Arrival = kDevWarpPresetAll;
  } else {
    size_t used = 0;
    unsigned long entrance = 0;
    try {
      entrance = std::stoul(head, &used, 0);
    } catch (const std::exception &) {
      used = 0;
    }
    if (head.empty() || used != head.size() || entrance > 0xFFFF)
      throw bad("invalid entrance '" + head + "'");
    if (std::none_of(entrances.begin(), entrances.end(), [&](const auto &e) { return e.Entrance == entrance; }))
      throw bad("entrance '" + head + "' is absent from the catalog");
    script.Entrance = uint16_t(entrance);
  }
  for (size_t index = 1; index < fields.size(); ++index) {
    const std::string field(fields[index]);
    const auto equals = field.find('=');
    const std::string key = field.substr(0, equals), value = equals == std::string::npos ? "" : field.substr(equals + 1);
    if (key == "day" && (value == "1" || value == "2" || value == "3")) {
      script.Arrival[kMm3dAxisDay] = value[0] - '0';
    } else if (key == "time" && value == "keep") {
      script.Arrival[kMm3dAxisTime] = kDevWarpKeep;
    } else if (key == "time" && value.size() == 4 && std::all_of(value.begin(), value.end(), ::isdigit)) {
      const int hours = std::stoi(value.substr(0, 2)), minutes = std::stoi(value.substr(2));
      if (hours > 23 || minutes > 59)
        throw bad("invalid time '" + value + "'");
      script.Arrival[kMm3dAxisTime] = Mm3dDevAbi::Clock(hours, minutes);
    } else if (key == "form") {
      const auto found = std::find_if(std::begin(kMm3dFormChoices), std::end(kMm3dFormChoices), [&](const auto &c) {
        std::string label(c.Label);
        std::transform(label.begin(), label.end(), label.begin(), ::tolower);
        return label == value || (value == "deity" && c.Value == Mm3dDevAbi::FormDeity);
      });
      if (value == "keep")
        script.Arrival[kMm3dAxisForm] = kDevWarpKeep;
      else if (found == std::end(kMm3dFormChoices))
        throw bad("invalid form '" + value + "'");
      else
        script.Arrival[kMm3dAxisForm] = found->Value;
    } else if (key == "day" && value == "keep") {
      script.Arrival[kMm3dAxisDay] = kDevWarpKeep;
    } else {
      throw bad("invalid option '" + field + "'");
    }
  }
  return script;
}

class Mm3dDevTools final : public DevWarpTitleAbi {
  Mm3dDevProof mProof;
  std::array<DevWarpAxis, 3> mAxes;
  mutable uint32_t mLastCameraPlay = 0, mLastCameraFrame = 0;
  mutable int16_t mLastCameraScene = -1;

public:
  explicit Mm3dDevTools(Mm3dDevProof proof = kMm3dDevProof)
      : mProof(proof),
        mAxes{{{"day", "Day", kMm3dDayChoices, proof.SceneTransition && proof.Day, "day writes are awaiting proof"},
               {"time", "Time", kMm3dTimeChoices, proof.SceneTransition && proof.TimeOfDay,
                "time writes are awaiting proof"},
               {"form", "Form", kMm3dFormChoices, proof.SceneTransition && proof.PlayerForm,
                "form writes are awaiting proof"}}} {}
  std::span<const DevWarpEntrance> Entrances() const override { return kMm3dDevEntrances; }
  std::span<const DevWarpScene> Scenes() const override { return kMm3dDevScenes; }
  std::span<const DevWarpAxis> Axes() const override { return mAxes; }
  std::span<const DevWarpZone> Zones() const override { return kMm3dDevZones; }
  bool CameraAvailable() const override { return mProof.FreeCamera; }
  bool PointsAvailable() const override { return mProof.SceneTransition && mProof.WarpPointRespawn; }
  uint32_t PlayStateAddress(DevWarpGuest &g) const override { return Mm3dDevAbi::Play(g); }
  bool Observe(DevWarpGuest &g, uint32_t p, DevWarpObservation &o, int16_t &busy, uint32_t &player) const override {
    using namespace Mm3dDevAbi;
    o = {};
    player = 0;
    busy = -1;
    if (!p || p != Play(g) || !CodeMatches(g))
      return false;
    uint8_t state = 0, trigger = 0, mode = 0, form = 0;
    uint16_t overrideEntrance = 0, pause = 0, camIndex = 0;
    int32_t gameMode = 0, cutscene = 0, day = 0;
    if (!Read(g, p + 0x13C, state) || !Read(g, p + 0x148, o.Scene) || !Read(g, p + 0xC260, o.Room) ||
        !Read(g, p + 0x20DC, player) || !Read(g, p + 0xC529, trigger) || !Read(g, p + 0xC85A, mode) ||
        !Read(g, p + 0xCCAA, overrideEntrance) || !Read(g, p + 0xAAC, pause) || !Read(g, p + 0xA7A, camIndex) ||
        !Read(g, Common + 0x3518, o.Entrance) || !Read(g, Common + 0x20, o.DayTime) || !Read(g, Common + 0x26, form) ||
        !Read(g, Common + 0x1361C, gameMode) || !Read(g, Common + 0x1C, cutscene) || !Read(g, Day, day))
      return false;
    o.PlayState = true;
    o.Arrival = {day, o.DayTime, form, kDevWarpKeep};
    uint16_t health = 0;
    o.Health = Read(g, Health, health) ? health : -1;
    o.Age = -1;
    o.AgeOnLoad = -1;
    busy = (trigger || mode || overrideEntrance) ? 0 : -1;
    uint16_t actorId = 0;
    uint8_t category = 0;
    if (Pointer(player, 0x1F8) && Read(g, player, actorId) && actorId == 0 && Read(g, player + 2, category) &&
        category == 2) {
      o.Player = Read(g, player + 0x24, o.Position) && Read(g, player + 0xC2, o.RotY);
      for (float x : o.Position)
        o.Player = o.Player && std::isfinite(x);
    }
    o.Gameplay = state == 2 && gameMode == 0 && cutscene >= 0 && cutscene < 0xFFF0 && form <= 4 && !pause &&
                 !camIndex && busy < 0 && o.Player;
    return true;
  }
  bool ReadEntranceTable(DevWarpGuest &g, std::vector<uint32_t> &table) const override {
    if (!Mm3dDevAbi::CodeMatches(g))
      return false;
    std::vector<uint32_t> next(65536, UINT32_MAX);
    for (const auto &e : Entrances()) {
      uint32_t row = 0;
      if (!Mm3dDevAbi::Resolve(g, e.Entrance, row))
        return false;
      next[e.Entrance] = row;
    }
    table = std::move(next);
    return true;
  }
  DevWarpAvailability Check(const DevWarpEntrance &e, const std::vector<uint32_t> &table) const override {
    if (table.empty())
      return {};
    if (e.Grotto >= 0 || e.Entrance >= table.size() ||
        table[e.Entrance] != (uint32_t(e.Scene) | (uint32_t(e.Spawn) << 8)))
      return {true, false, "MM3D entrance table does not match this destination."};
    if (!mProof.SceneTransition)
      return {true, false, "Scene warps are awaiting gameplay validation."};
    return {true, true, {}};
  }
  bool Apply(DevWarpGuest &g, uint32_t p, uint16_t entrance, const DevWarpPoint *point,
             const DevWarpSelection &arrival, std::string *error) const override {
    using namespace Mm3dDevAbi;
    auto fail = [&](const char *reason) {
      if (error)
        *error = reason;
      return false;
    };
    if (!mProof.SceneTransition)
      return fail("MM3D transition proof pending");
    const int32_t day = arrival[kMm3dAxisDay], time = arrival[kMm3dAxisTime], form = arrival[kMm3dAxisForm];
    if (arrival[3] != kDevWarpKeep)
      return fail("MM3D has three arrival options");
    if (day != kDevWarpKeep && (!mAxes[kMm3dAxisDay].Proven || day < 1 || day > 3))
      return fail(mAxes[kMm3dAxisDay].Proven ? "MM3D day must be 1-3" : "MM3D day writes are awaiting proof");
    if (time != kDevWarpKeep && (!mAxes[kMm3dAxisTime].Proven || time < 0 || time > 0xFFFF))
      return fail(mAxes[kMm3dAxisTime].Proven ? "MM3D time must be 0x0000-0xFFFF" : "MM3D time writes are awaiting proof");
    if (form != kDevWarpKeep && (!mAxes[kMm3dAxisForm].Proven || form < FormDeity || form > FormHuman))
      return fail(mAxes[kMm3dAxisForm].Proven ? "MM3D form must be 0-4" : "MM3D form writes are awaiting proof");
    if (point && !point->UseSceneEntrance && !mProof.WarpPointRespawn)
      return fail("MM3D point-respawn proof pending");
    DevWarpObservation o;
    int16_t busy = -1;
    uint32_t player = 0, row = 0;
    if (!Observe(g, p, o, busy, player) || !o.Gameplay || busy >= 0)
      return fail("MM3D is not idle gameplay");
    const auto catalog = Entrances();
    const auto found =
        std::find_if(catalog.begin(), catalog.end(), [&](const auto &e) { return e.Entrance == entrance; });
    if (found == catalog.end() || !Resolve(g, entrance, row) ||
        row != (uint32_t(found->Scene) | (uint32_t(found->Spawn) << 8)))
      return fail("MM3D live destination no longer matches catalog");
    int32_t layer = 0, oldRespawn = 0;
    uint16_t nextCutscene = 0;
    if (!Read(g, Common + 0x13620, layer) || layer != 0 || !Read(g, Common + 0x13624, oldRespawn) || oldRespawn != 0 ||
        !Read(g, Common + 0x369E, nextCutscene) || nextCutscene != 0xFFEF)
      return fail("MM3D has a staged layer/cutscene/respawn");
    if (form == FormDeity && !DeityAllowed(found->Scene))
      return fail("Fierce Deity is allowed only in the boss lairs");
    uint16_t nextDayTime = 0;
    if ((day != kDevWarpKeep || time != kDevWarpKeep) && (!Read(g, NextDayTime, nextDayTime) || nextDayTime != 0xFFFF))
      return fail("MM3D has a staged day change (nextDayTime)");
    Transaction tx(g);
    // The arrival options, written before the transition is published; the
    // new scene's Play_Init derives isNight and the sky from the time.
    // eventDayCount stays as it is (2S2H's map select does the same).
    if ((day != kDevWarpKeep && !tx.Add(Day, day)) || (time != kDevWarpKeep && !tx.Add(Time, uint16_t(time))) ||
        (form != kDevWarpKeep && (!tx.Add(Form, uint8_t(form)) || !tx.Add(EquippedMask, uint8_t{0}))))
      return fail("unreadable arrival fields");
    int32_t respawn = 0;
    if (point && !point->UseSceneEntrance) {
      for (float x : point->Position)
        if (!std::isfinite(x))
          return fail("non-finite point");
      entrance = uint16_t(entrance & 0xFE0F); // native respawn uses selector zero
      if (!Resolve(g, entrance, row) || (row & 255) != found->Scene)
        return fail("invalid respawn entrance");
      if (!tx.Add(Common + 0x13628, point->Position) || !tx.Add(Common + 0x13634, point->RotY) ||
          !tx.Add(Common + 0x13636, uint16_t{0x0DFF}) || !tx.Add(Common + 0x13638, entrance) ||
          !tx.Add(Common + 0x1363A, point->RoomNum) || !tx.Add(Common + 0x13763, uint8_t{2}))
        return fail("unreadable respawn fields");
      respawn = -8;
    }
    // Publication is last. Native Play_Update prepares its own transition.
    // Natural door traces preserve nextCutsceneIndex=0xFFEF. Preserve it here.
    if (!tx.Add(Common + 0x13624, respawn) || !tx.Add(p + 0xC52E, entrance) || !tx.Add(p + 0xC534, uint8_t{2}) ||
        !tx.Add(p + 0xC535, uint8_t{0}) || !tx.Add(p + 0xC529, uint8_t{20}))
      return fail("unreadable transition fields");
    return tx.Commit() || fail("MM3D write failed and was rolled back");
  }
  // The runtime has already seen the transition end in the requested scene
  // with the player present. An entrance cutscene (a boss lair's intro:
  // Twinmold's runs past the 900-frame arrival window) is part of arriving,
  // so the arrival does not wait for gameplay to resume.
  bool Arrived(const DevWarpObservation &o, uint16_t entrance, bool point) const override {
    return o.Player && o.Entrance == (point ? uint16_t(entrance & 0xFE0F) : entrance);
  }
  // The clock runs during the transition: an arrival within this many units
  // (about 45 game minutes) of the requested time counts.
  static constexpr uint16_t kTimeTolerance = 0x0800;
  std::string ArrivalMismatch(DevWarpGuest &g, const DevWarpObservation &o,
                              const DevWarpSelection &arrival) const override {
    using namespace Mm3dDevAbi;
    int32_t day = 0;
    uint16_t time = 0;
    uint8_t form = 0, mask = 0, night = 0;
    if (!Read(g, Day, day) || !Read(g, Time, time) || !Read(g, Form, form) || !Read(g, EquippedMask, mask) ||
        !Read(g, IsNight, night))
      return "field=save unreadable";
    char text[96];
    if (arrival[kMm3dAxisDay] != kDevWarpKeep && day != arrival[kMm3dAxisDay]) {
      std::snprintf(text, sizeof(text), "field=day expected=%d actual=%d", arrival[kMm3dAxisDay], day);
      return text;
    }
    if (arrival[kMm3dAxisTime] != kDevWarpKeep) {
      if (uint16_t(time - uint16_t(arrival[kMm3dAxisTime])) > kTimeTolerance) {
        std::snprintf(text, sizeof(text), "field=time expected=0x%04X actual=0x%04X", arrival[kMm3dAxisTime], time);
        return text;
      }
      if ((night != 0) != NightAt(time)) {
        std::snprintf(text, sizeof(text), "field=night expected=%d actual=%u time=0x%04X", NightAt(time) ? 1 : 0, night,
                      time);
        return text;
      }
    }
    if (arrival[kMm3dAxisForm] != kDevWarpKeep && (form != arrival[kMm3dAxisForm] || mask != 0)) {
      std::snprintf(text, sizeof(text), "field=form expected=%d actual=%u mask=%u", arrival[kMm3dAxisForm], form, mask);
      return text;
    }
    (void)o;
    return {};
  }
  std::string Describe(const DevWarpObservation &o) const override {
    if (!o.PlayState)
      return {};
    std::string text = "Day " + std::to_string(o.Arrival[kMm3dAxisDay]) + " " +
                       Mm3dDevAbi::ClockText(uint16_t(o.Arrival[kMm3dAxisTime])) + ", " +
                       Mm3dDevAbi::FormName(o.Arrival[kMm3dAxisForm]);
    if (o.Health >= 0)
      text += ", health " + std::to_string(o.Health);
    return text;
  }
  bool CameraOwnsInput(DevWarpGuest &g) const override {
    using namespace Mm3dDevAbi;
    if (!mProof.FreeCamera)
      return false;
    const uint32_t p = Play(g);
    DevWarpObservation o;
    int16_t busy = -1;
    uint32_t actor = 0, slot = 0;
    uint16_t id = 1, status = 0, setting = 0;
    return Observe(g, p, o, busy, actor) && o.Gameplay && Read(g, p + 0xA68, slot) && slot == p + 0x408 &&
           Read(g, slot + 0x178, id) && id == 0 && Read(g, slot + 0x154, status) && status == 7 &&
           Read(g, slot + 0x156, setting) && setting != 9 && setting != 10;
  }
  bool ApplyCamera(DevWarpGuest &g, DevFreeCamera &free, uint32_t out, uint32_t camera) const override {
    using namespace Mm3dDevAbi;
    if (!free.Active() || !CameraOwnsInput(g))
      return false;
    const uint32_t p = Play(g);
    if (camera != p + 0x408 || !Pointer(out, 6))
      return false;
    uint32_t frame = 0;
    int16_t scene = -1;
    if (!Read(g, p + 0x138, frame) || !Read(g, p + 0x148, scene))
      return false;
    if (mLastCameraPlay && (p != mLastCameraPlay || scene != mLastCameraScene || frame < mLastCameraFrame))
      free.ResetToGame();
    std::array<float, 3> eye{}, at{};
    if (!Read(g, camera + 0x5C, eye) || !Read(g, camera + 0x50, at) || !Finite(eye) || !Finite(at))
      return false;
    const auto view = free.ResolveView(eye, at, false);
    if (!view || !Finite(view->Eye) || !Finite(view->At))
      return false;
    Transaction tx(g);
    const std::array<float, 3> up{0, 1, 0};
    const std::array<int16_t, 3> direction{view->Pitch, view->Yaw, 0};
    if (!tx.Add(camera + 0x50, view->At) || !tx.Add(camera + 0x5C, view->Eye) || !tx.Add(camera + 0x68, up) ||
        !tx.Add(camera + 0x74, view->Eye) || !tx.Add(p + 0x220, view->Eye) || !tx.Add(p + 0x22C, view->At) ||
        !tx.Add(p + 0x238, up) || !tx.Add(camera + 0x148, direction) || !tx.Add(camera + 0x14E, direction) ||
        !tx.Add(out, direction) || !tx.Commit())
      return false;
    free.MarkApplied(*view);
    mLastCameraPlay = p;
    mLastCameraScene = scene;
    mLastCameraFrame = frame;
    return true;
  }
};
} // namespace Oot3dNativeGame
#endif
