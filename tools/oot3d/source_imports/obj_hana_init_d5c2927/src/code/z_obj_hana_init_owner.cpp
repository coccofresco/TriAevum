#include "oot3d/obj_hana_init_owner.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Oot3dSourceObjHanaInit {
namespace {

template <typename T>
T ReadValue(const void* base, size_t offset = 0U) {
    T value{};
    std::memcpy(
        &value, static_cast<const uint8_t*>(base) + offset,
        sizeof(value));
    return value;
}

template <typename T>
void WriteValue(void* base, size_t offset, T value) {
    std::memcpy(
        static_cast<uint8_t*>(base) + offset, &value,
        sizeof(value));
}

constexpr size_t kObjHanaSize = 0x200U;
constexpr size_t kRecordSize = 0x10U;

} // namespace

// Revision-pinned semantic transfer of Actor_LoadModelList at 0x00372F38.
// This overload represents its one-pair variadic ABI at the ObjHana callsite.
void ModelHandle_LoadAll(
    uint32_t actorAddress, uint32_t playAddress,
    uint32_t modelHandleAddress, uint32_t modelIndex,
    uint32_t terminator) {
    if (terminator != 0U) {
        FailSourceCallback(
            "ObjHana Actor_LoadModelList terminator is not zero");
    }

    const auto& literal = ActiveLiterals();
    auto* actor = ResolveGuestWrite(actorAddress, kObjHanaSize);
    WriteValue<uint8_t>(actor, 0x19AU, 1U);

    uint32_t archiveBase = 0U;
    const uint8_t objectSlot = ReadValue<uint8_t>(actor, 0x1EU);
    if (objectSlot < 0x13U) {
        const uint32_t slotAddress =
            playAddress + static_cast<uint32_t>(objectSlot) * 0x80U;
        const auto* ready = ResolveGuestRead(
            slotAddress + literal.ObjectReadyOffset, sizeof(uint32_t));
        if (ReadValue<uint32_t>(ready) != 0U) {
            archiveBase =
                slotAddress + literal.ObjectReadyOffset - 8U;
        }
    }
    const uint32_t archiveAddress = archiveBase + 0x10U;

    const auto* guard = ResolveGuestRead(
        literal.RendererInitGuard, sizeof(uint32_t));
    if ((ReadValue<uint32_t>(guard) & 1U) == 0U &&
        StaticInitGuardAcquire(literal.RendererInitGuard) != 0) {
        RendererGlobalState_Init(literal.RendererGlobalState);
    }

    const auto* factorySlot = ResolveGuestRead(
        literal.RendererGlobalState + 0x17CU, sizeof(uint32_t));
    const uint32_t factoryAddress =
        ReadValue<uint32_t>(factorySlot);
    auto* factory =
        ResolveGuestWrite(factoryAddress, 0x0CU);
    const uint32_t modelContext =
        ReadValue<uint32_t>(actor, 0x178U);
    WriteValue(factory, 0x08U, modelContext);

    uint32_t loaded = 0U;
    if (static_cast<int32_t>(modelIndex) >= 0) {
        const uint32_t cmbAddress =
            ZAR_GetCMBByIndex(archiveAddress, modelIndex);
        const uint32_t modelAddress =
            RendererFactoryCreate(factoryAddress, cmbAddress, 1U);
        auto* modelHandle = ResolveGuestWrite(
            modelHandleAddress, sizeof(uint32_t));
        WriteValue(modelHandle, 0U, modelAddress);

        auto* stats = ResolveGuestWrite(
            literal.ModelLoadStats, 2U * sizeof(uint32_t));
        WriteValue(
            stats, 0U, ReadValue<uint32_t>(stats) + 1U);
        ++loaded;
    }
    WriteValue(factory, 0x08U, 0U);
    if (loaded != 0U) {
        auto* stats = ResolveGuestWrite(
            literal.ModelLoadStats, 2U * sizeof(uint32_t));
        WriteValue(
            stats, sizeof(uint32_t),
            ReadValue<uint32_t>(stats, sizeof(uint32_t)) + 1U);
    }
}

// Revision-pinned semantic transfer of ObjHana_Init at 0x001E1734 from
// producer commit d5c2927. Call order, guest offsets and target literals
// follow code.bin; the N64 source is used only to name the recovered fields.
void ObjHana_Init(uint32_t actorAddress, uint32_t playAddress) {
    const auto& literal = ActiveLiterals();
    auto* actor = ResolveGuestWrite(actorAddress, kObjHanaSize);
    const uint32_t selector =
        ReadValue<uint16_t>(actor, 0x1CU) & 3U;
    const uint32_t recordAddress =
        literal.ModelRecords + selector * kRecordSize;
    const auto* record =
        ResolveGuestRead(recordAddress, kRecordSize);

    const uint32_t modelIndex = ReadValue<uint32_t>(record);
    const float scale = ReadValue<float>(record, 0x04U);
    const float yOffset = ReadValue<float>(record, 0x08U);
    const int16_t radius = ReadValue<int16_t>(record, 0x0CU);
    const int16_t height = ReadValue<int16_t>(record, 0x0EU);

    ModelHandle_LoadAll(
        actorAddress, playAddress, actorAddress + 0x1FCU,
        modelIndex, 0U);
    Actor_ProcessInitChain(actorAddress, literal.InitChain);
    Actor_SetScale(actorAddress, scale);
    WriteValue(actor, 0xC4U, yOffset);

    if (radius >= 0) {
        constexpr uint32_t colliderOffset = 0x1A4U;
        Collider_InitCylinder(
            playAddress, actorAddress + colliderOffset);
        Collider_SetCylinder(
            playAddress, actorAddress + colliderOffset,
            actorAddress, literal.CylinderInit);
        Collider_UpdateCylinder(
            actorAddress, actorAddress + colliderOffset);
        WriteValue(actor, 0x1E4U, static_cast<float>(radius));
        WriteValue(actor, 0x1E8U, static_cast<float>(height));
        CollisionCheck_SetInfo(
            actorAddress + 0xA0U, 0U,
            literal.CollisionInfoInit);
    }

    if (selector == 2U) {
        const auto* saveContext =
            ResolveGuestRead(literal.SaveContext + 0xF4U, 2U);
        if ((ReadValue<uint16_t>(saveContext) & 1U) != 0U) {
            Actor_Kill(actorAddress);
        }
    }
}

} // namespace Oot3dSourceObjHanaInit
