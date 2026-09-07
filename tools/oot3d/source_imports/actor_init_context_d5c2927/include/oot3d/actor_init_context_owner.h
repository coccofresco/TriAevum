#pragma once

#include <cstddef>
#include <cstdint>

namespace Oot3dSourceActorInitContext {

struct ActorInitContextLiterals {
    uint32_t DAT_0044ecac = 0U;
    uint32_t DAT_0044ecb0 = 0U;
    uint32_t DAT_0044ecb4 = 0U;
    uint32_t DAT_0044ecc0 = 0U;
    uint32_t DAT_0044ecc4 = 0U;
    uint32_t DAT_0044ecc8 = 0U;
    uint32_t DAT_0044eccc = 0U;
    uint32_t DAT_0044ecd0 = 0U;
    uint32_t DAT_0044ecd4 = 0U;
    uint32_t DAT_0044ecd8 = 0U;
    uint32_t DAT_0044ecdc = 0U;
    uint32_t DAT_0044efac = 0U;
    uint32_t DAT_0044efa8 = 0U;
    uint32_t DAT_0044efe8 = 0U;
    uint32_t DAT_0044efec = 0U;
    uint32_t DAT_0044eff0 = 0U;
    uint32_t DAT_0044eff4 = 0U;
    uint32_t DAT_0044eff8 = 0U;
    uint32_t DAT_0044effc = 0U;
    uint32_t DAT_0044f000 = 0U;
    uint32_t DAT_0044f004 = 0U;
    uint32_t DAT_0044f008 = 0U;
    uint32_t DAT_00463420 = 0U;
    uint32_t DAT_00463424 = 0U;
    uint32_t DAT_00463428 = 0U;
    uint32_t DAT_00463434 = 0U;
    uint32_t DAT_00463438 = 0U;
    uint32_t DAT_0046343c = 0U;
    uint32_t DAT_00463440 = 0U;
    uint32_t DAT_00463444 = 0U;
    uint32_t DAT_00463448 = 0U;
};

const ActorInitContextLiterals& ActiveLiterals();
void* ResolveGuestMemory(uint32_t address, size_t size);

uint32_t Actor_Spawn(
    uint32_t actorContext, uint32_t play, int32_t actorId,
    float x, float y, float z, int32_t rotX, int32_t rotY,
    int32_t rotZ, int32_t params, int32_t initializeNow);
void FUN_004644a8(uint32_t value, uint32_t play);
uint32_t oot3d_memclear(uint32_t address, uint32_t size);
void FUN_0045fe94();
uint32_t Object_GetIndex(uint32_t objectContext, uint32_t objectId);
uint32_t ZAR_GetCMBByIndex(uint32_t archive, uint32_t index);
uint32_t ZAR_GetCTXBByIndex(uint32_t archive, uint32_t index);
int32_t oot3d_static_init_guard_acquire(uint32_t guardAddress);
void RendererGlobalState_Init(uint32_t rendererStateAddress);
uint32_t RendererFactoryAddress(uint32_t rendererStateAddress);
uint32_t RendererFactoryCreate(
    uint32_t factoryAddress, uint32_t resourceAddress, uint32_t flags);
void oot3d_store_child14_byte16(uint32_t objectAddress, uint8_t value);
void Lights_PointNoGlowSetInfo(
    uint32_t infoAddress, float x, float y, float z,
    uint32_t red, uint32_t green, uint32_t blue,
    int32_t radius, uint32_t attenuation);
uint32_t LightContext_InsertLight(
    uint32_t play, uint32_t lightContext, uint32_t infoAddress);
uint32_t AllocatorAllocate(
    uint32_t allocatorGlobalAddress, uint32_t size,
    uint32_t sourcePathAddress, uint32_t sourceLine);
uint32_t FUN_00348f34(uint32_t objectAddress, uint32_t profileAddress);
uint32_t FUN_00348be4(uint32_t objectAddress);
void FUN_00348a64(
    uint32_t layoutAddress, int32_t streamIndex, uint32_t sourceAddress,
    uint32_t semantic, uint32_t componentType, uint32_t inputType,
    uint32_t outputType);
uint32_t FUN_0034897c(
    uint32_t rootAddress, uint32_t textureAddress,
    uint32_t modelContextAddress, uint32_t transferStateAddress);
void FUN_003429c8(
    uint32_t materialAddress, uint32_t enabled,
    const uint32_t parameters[4]);
uint32_t oot3d_get_indexed_field_58_entry(
    uint32_t archive, uint32_t index);
void FUN_00372d94(uint32_t objectAddress, uint32_t resourceAddress);

void Actor_InitContext(
    uint32_t playAddress, uint32_t actorContextAddress,
    const int16_t* playerSpawnEntry);

} // namespace Oot3dSourceActorInitContext
