#pragma once
#if defined(TRIAEVUM_DEVELOPER_TOOLS)
#include "oot3d_dev_free_camera.h"
#include "oot3d_dev_warp.h"
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace Oot3dNativeGame {
// These are integration proof gates, not user preferences. Set each only after
// the corresponding DEVWARP ABI live receipt and mutation smoke is reviewed.
struct Mm3dDevProof {
  bool SceneTransition = false;
  bool FreeCamera = false;
  bool WarpPointRespawn = false;
};
inline constexpr Mm3dDevProof kMm3dDevProof{true, true, false};

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

class Mm3dDevTools final : public DevWarpTitleAbi {
  Mm3dDevProof mProof;
  mutable uint32_t mLastCameraPlay = 0, mLastCameraFrame = 0;
  mutable int16_t mLastCameraScene = -1;

public:
  explicit Mm3dDevTools(Mm3dDevProof proof = kMm3dDevProof) : mProof(proof) {}
  std::span<const DevWarpEntrance> Entrances() const override { return kMm3dDevEntrances; }
  std::span<const DevWarpScene> Scenes() const override { return kMm3dDevScenes; }
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
    int32_t gameMode = 0, cutscene = 0;
    if (!Read(g, p + 0x13C, state) || !Read(g, p + 0x148, o.Scene) || !Read(g, p + 0xC260, o.Room) ||
        !Read(g, p + 0x20DC, player) || !Read(g, p + 0xC529, trigger) || !Read(g, p + 0xC85A, mode) ||
        !Read(g, p + 0xCCAA, overrideEntrance) || !Read(g, p + 0xAAC, pause) || !Read(g, p + 0xA7A, camIndex) ||
        !Read(g, Common + 0x3518, o.Entrance) || !Read(g, Common + 0x20, o.DayTime) || !Read(g, Common + 0x26, form) ||
        !Read(g, Common + 0x1361C, gameMode) || !Read(g, Common + 0x1C, cutscene))
      return false;
    o.PlayState = true;
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
  bool Apply(DevWarpGuest &g, uint32_t p, uint16_t entrance, const DevWarpPoint *point, std::optional<DevWarpAge>,
             std::optional<bool> night, std::string *error) const override {
    using namespace Mm3dDevAbi;
    auto fail = [&](const char *reason) {
      if (error)
        *error = reason;
      return false;
    };
    if (!mProof.SceneTransition)
      return fail("MM3D transition proof pending");
    if (night)
      return fail("MM3D scene warp preserves the current day, time and form");
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
    Transaction tx(g);
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
  bool Arrived(const DevWarpObservation &o, uint16_t entrance, bool point) const override {
    return o.Gameplay && o.Entrance == (point ? uint16_t(entrance & 0xFE0F) : entrance);
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
