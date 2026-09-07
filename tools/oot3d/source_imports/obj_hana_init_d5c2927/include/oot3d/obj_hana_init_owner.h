#pragma once

#include <cstddef>
#include <cstdint>

namespace Oot3dSourceObjHanaInit {

struct ObjHanaInitLiterals {
    uint32_t ModelRecords = 0U;
    uint32_t InitChain = 0U;
    uint32_t CylinderInit = 0U;
    uint32_t CollisionInfoInit = 0U;
    uint32_t SaveContext = 0U;
    uint32_t ObjectReadyOffset = 0U;
    uint32_t RendererInitGuard = 0U;
    uint32_t RendererGlobalState = 0U;
    uint32_t ModelLoadStats = 0U;
};

const ObjHanaInitLiterals& ActiveLiterals();
const void* ResolveGuestRead(uint32_t address, size_t size);
void* ResolveGuestWrite(uint32_t address, size_t size);
[[noreturn]] void FailSourceCallback(const char* message);

int32_t StaticInitGuardAcquire(uint32_t guardAddress);
void RendererGlobalState_Init(uint32_t rendererStateAddress);
uint32_t ZAR_GetCMBByIndex(uint32_t archiveAddress, uint32_t modelIndex);
uint32_t RendererFactoryCreate(
    uint32_t factoryAddress, uint32_t resourceAddress, uint32_t flags);

void ModelHandle_LoadAll(
    uint32_t actorAddress, uint32_t playAddress,
    uint32_t modelHandleAddress, uint32_t modelIndex,
    uint32_t terminator);
void Actor_ProcessInitChain(
    uint32_t actorAddress, uint32_t initChainAddress);
void Actor_SetScale(uint32_t actorAddress, float scale);
void Collider_InitCylinder(
    uint32_t playAddress, uint32_t colliderAddress);
void Collider_SetCylinder(
    uint32_t playAddress, uint32_t colliderAddress,
    uint32_t actorAddress, uint32_t initAddress);
void Collider_UpdateCylinder(
    uint32_t actorAddress, uint32_t colliderAddress);
void CollisionCheck_SetInfo(
    uint32_t collisionInfoAddress, uint32_t damageTableAddress,
    uint32_t initAddress);
void Actor_Kill(uint32_t actorAddress);

void ObjHana_Init(uint32_t actorAddress, uint32_t playAddress);

} // namespace Oot3dSourceObjHanaInit
