#ifndef OOT3D_FOG_MATERIAL_SCALAR_H
#define OOT3D_FOG_MATERIAL_SCALAR_H

#include "oot3d/types.h"

#define OOT3D_FOG_MATERIAL_TEMPLATE_ADDR 0x004FA8B8u
#define OOT3D_FOG_MATERIAL_INV_255_ADDR 0x00464D58u
#define OOT3D_FOG_MATERIAL_ALLOCATOR_SLOT_ADDR 0x0055A1E4u
#define OOT3D_FOG_MATERIAL_LUT_COUNTER_ADDR 0x0054CC2Cu
#define OOT3D_FOG_MATERIAL_RUNTIME_SIZE 0x668u
#define OOT3D_FOG_MATERIAL_TEMPLATE_WORD_COUNT 21u
#define OOT3D_FOG_LUT_ENTRY_COUNT 128u
#define OOT3D_FOG_CURVE_SAMPLE_COUNT 129u
#define OOT3D_FOG_VIEW_WORD_COUNT 16u
#define OOT3D_FOG_VIEW_DERIVED_TERM_COUNT 16u
#define OOT3D_PLAY_FOG_MATERIAL_SOURCE_OFFSET 0x5FC8u
#define OOT3D_PLAY_VIEW_SOURCE_OFFSET 0x01DCu
#define OOT3D_PLAY_FINAL_FOG_RGB_OFFSET 0x0A82u
#define OOT3D_PLAY_FOG_FAR_OFFSET 0x0A78u
#define OOT3D_PLAY_FOG_NEAR_OFFSET 0x0A7Cu
#define OOT3D_PLAY_MATERIAL_RUNTIME_LIST0_OFFSET 0x4C30u
#define OOT3D_PLAY_MATERIAL_RUNTIME_LIST1_OFFSET 0x500Cu
#define OOT3D_GAMEPLAY_FOG_MATERIAL_UPDATE_CALLSITE 0x002E2674u
#define OOT3D_GAMEPLAY_MATERIAL_RUNTIME_LIST0_BIND_CALLSITE 0x002E2C20u
#define OOT3D_GAMEPLAY_MATERIAL_RUNTIME_LIST1_BIND_CALLSITE 0x002E2C34u
#define OOT3D_GAMEPLAY_FOG_ALPHA_IMMEDIATE 0xFFu
#define OOT3D_MATERIAL_PACKET_ENABLE_OFFSET 0x0Au
#define OOT3D_MATERIAL_PACKET_FOG_R_OFFSET 0x6Cu
#define OOT3D_MATERIAL_PACKET_FOG_G_OFFSET 0x70u
#define OOT3D_MATERIAL_PACKET_FOG_B_OFFSET 0x74u
#define OOT3D_MATERIAL_PACKET_LUT_GENERATION_OFFSET 0x7Cu
#define OOT3D_MATERIAL_PACKET_LUT_TABLE_OFFSET 0x80u
#define OOT3D_MATERIAL_RUNTIME_LIST_COUNT_OFFSET 0x06u
#define OOT3D_MATERIAL_RUNTIME_LIST_ENTRY_STRIDE 0x3Cu
#define OOT3D_MATERIAL_RUNTIME_LIST_ENTRY_OBJECT_OFFSET 0x0Cu
#define OOT3D_MATERIAL_RUNTIME_OBJECT_PACKET_OWNER_OFFSET 0x14u
#define OOT3D_MATERIAL_PACKET_OWNER_PACKET_OFFSET 0x10u

typedef float (*Oot3dFogCurveTransform)(float value, void* user);
typedef u8* (*Oot3dMaterialScalarAddressResolver)(u32 runtimeAddress, size_t minimumSize, void* user);

enum {
    OOT3D_FOG_UPDATE_ALLOCATED = 1 << 0,
    OOT3D_FOG_UPDATE_COLOR_CHANGED = 1 << 1,
    OOT3D_FOG_UPDATE_VIEW_CHANGED = 1 << 2,
    OOT3D_FOG_UPDATE_NEAR_FAR_CHANGED = 1 << 3,
    OOT3D_FOG_UPDATE_ENABLED_CHANGED = 1 << 4,
    OOT3D_FOG_UPDATE_REBUILT = 1 << 5,
};

typedef struct {
    u32 rawWords[OOT3D_FOG_MATERIAL_TEMPLATE_WORD_COUNT];
} Oot3dFogMaterialTemplateWords;

typedef struct {
    float fogColorR;
    float fogColorG;
    float fogColorB;
    float fogColorA;
    s32 nearZ;
    s32 farZ;
    s32 curveScale;
    u8 rebuildDisabled;
    u8 curveMode;
} Oot3dFogMaterialTemplateDecoded;

typedef struct {
    u32 updateFunction;
    u32 bindFunction;
    u32 runtimeListBindFunction;
    u32 runtimeObjectMaterialPacketResolverFunction;
    u32 runtimeInitFunction;
    u32 runtimeApplyFunction;
    u32 packedTableRebuildFunction;
    u32 viewCopyFunction;
    u32 viewCompareFunction;
    u32 fogCurveBuilderFunction;
    u32 fogPackBaseFunction;
    u32 fogPackSlopeFunction;
    u32 materialStateCopyFogRgbFunction;
    u32 templateAddress;
    u32 allocationSize;
    u32 allocatorSlotAddress;
    u32 lutGenerationCounterAddress;
    u32 playFogSourceOffset;
    u32 playMaterialRuntimeList0Offset;
    u32 playMaterialRuntimeList1Offset;
    u32 runtimeMaterialListCountOffset;
    u32 runtimeMaterialListEntryStride;
    u32 runtimeMaterialListEntryObjectOffset;
    u32 runtimeObjectPacketOwnerOffset;
    u32 packetOwnerMaterialPacketOffset;
    u32 inv255Word;
    float inv255;
} Oot3dFogMaterialScalarMetadata;

typedef struct {
    u32 gameplayDrawFunction;
    u32 fogMaterialUpdateCallsite;
    u32 materialRuntimeList0BindCallsite;
    u32 materialRuntimeList1BindCallsite;
    u32 playFogSourceOffset;
    u32 playViewSourceOffset;
    u32 playFinalFogRgbOffset;
    u32 playFogFarFloatOffset;
    u32 playFogNearU16Offset;
    u32 playMaterialRuntimeList0Offset;
    u32 playMaterialRuntimeList1Offset;
    u8 fogAlphaImmediate;
    u8 pad[3];
} Oot3dGameplayFogMaterialOrder;

typedef struct {
    float packBaseZero;
    float packBaseScale;
    u32 packBaseMax;
    float packSlopeZero;
    float packSlopeBias;
    float packSlopeScale;
    float packSlopeMax;
    float packSlopeSplit;
    float packSlopeOffset;
    float buildCurveStep;
    float buildCurveFarValue;
    float buildCurveNearValue;
    float viewCopyDeterminantZero;
    float viewCopyInverseScale;
    float updateDisableDistance;
} Oot3dFogMaterialCurveConstants;

typedef struct {
    u32 runtimeAddr;
    u32 viewWords[OOT3D_FOG_VIEW_WORD_COUNT];
    u8 enabled;
    u8 dirty;
    u8 pad46[2];
} Oot3dFogMaterialSourceLayout;

typedef struct {
    u32 templateAddr;
    float fogColorR;
    float fogColorG;
    float fogColorB;
    float fogColorA;
    float nearZ;
    float farZ;
    u32 unknown1C;
    u32 unknown20;
    u32 lutGeneration;
    float viewDerivedTerms[OOT3D_FOG_VIEW_DERIVED_TERM_COUNT];
    float fogLutBase[OOT3D_FOG_LUT_ENTRY_COUNT];
    float fogLutDelta[OOT3D_FOG_LUT_ENTRY_COUNT];
    u32 packedPicaFogLut[OOT3D_FOG_LUT_ENTRY_COUNT];
} Oot3dFogMaterialRuntimeLayout;

typedef struct {
    Oot3dFogMaterialSourceLayout* source;
    Oot3dFogMaterialRuntimeLayout* runtime;
    u32 runtimeAddress;
    const float* view;
    float nearZ;
    float farZ;
    u8 fogR;
    u8 fogG;
    u8 fogB;
    u8 fogA;
    u32* lutGenerationCounter;
    const u8* runtimeList0;
    const u8* runtimeList1;
    Oot3dMaterialScalarAddressResolver resolver;
    void* resolverUser;
    const Oot3dFogMaterialTemplateDecoded* templateData;
    u32 packedPicaFogLutAddress;
    Oot3dFogCurveTransform transform;
    void* transformUser;
} Oot3dGameplayFogMaterialFrameInput;

typedef struct {
    int sourceUpdateFlags;
    int runtimeList0BoundCount;
    int runtimeList1BoundCount;
} Oot3dGameplayFogMaterialFrameResult;

extern const Oot3dFogMaterialTemplateWords gOot3dFogMaterialTemplateWords_004FA8B8;
extern const Oot3dFogMaterialTemplateDecoded gOot3dFogMaterialTemplateDecoded_004FA8B8;
extern const Oot3dFogMaterialScalarMetadata gOot3dFogMaterialScalarMetadata;
extern const Oot3dGameplayFogMaterialOrder gOot3dGameplayFogMaterialOrder;
extern const Oot3dFogMaterialCurveConstants gOot3dFogMaterialCurveConstants;

void Oot3d_FogCopyViewWords(u32 dst[OOT3D_FOG_VIEW_WORD_COUNT], const float src[OOT3D_FOG_VIEW_WORD_COUNT]);
int Oot3d_FogViewWordsChanged(const u32 lhs[OOT3D_FOG_VIEW_WORD_COUNT],
                               const u32 rhs[OOT3D_FOG_VIEW_WORD_COUNT]);
void Oot3d_FogCopyViewWordsToRuntime(Oot3dFogMaterialRuntimeLayout* runtime,
                                      const u32 viewWords[OOT3D_FOG_VIEW_WORD_COUNT]);
void Oot3d_FogCopyViewTermsToRuntime(Oot3dFogMaterialRuntimeLayout* runtime,
                                      const float view[OOT3D_FOG_VIEW_WORD_COUNT]);
u32 Oot3d_FogPackBase(float value);
u32 Oot3d_FogPackSlope(float value);
int Oot3d_FogBuildCurveArrays(Oot3dFogMaterialRuntimeLayout* runtime,
                                  const Oot3dFogMaterialTemplateDecoded* templateData,
                                  Oot3dFogCurveTransform transform, void* transformUser);
int Oot3d_FogRebuildPackedPicaTable(Oot3dFogMaterialRuntimeLayout* runtime,
                                     const Oot3dFogMaterialTemplateDecoded* templateData,
                                     Oot3dFogCurveTransform transform, void* transformUser);
void Oot3d_FogRuntimeInitFromTemplate(Oot3dFogMaterialRuntimeLayout* runtime, u32 templateAddress);
int Oot3d_FogRuntimeApplyTemplateAndBuildLut(Oot3dFogMaterialRuntimeLayout* runtime,
                                             const Oot3dFogMaterialTemplateDecoded* templateData,
                                             u32* lutGenerationCounter,
                                             Oot3dFogCurveTransform transform, void* transformUser);
void Oot3d_MaterialStateCopyFogRgb(u8* materialPacket, const Oot3dFogMaterialRuntimeLayout* runtime);
void Oot3d_FogSourceMarkMaterialStateEnabled(u8* materialPacket);
void Oot3d_FogBindRuntimeToMaterialPacket(u8* materialPacket, const Oot3dFogMaterialRuntimeLayout* runtime,
                                             u32 packedPicaFogLutAddress);
u32 Oot3d_MaterialScalarReadLeU32(const u8* bytes);
u8 Oot3d_MaterialScalarRuntimeListCount(const u8* runtimeList);
u32 Oot3d_MaterialScalarRuntimeListObjectAddress(const u8* runtimeList, u32 index);
u32 Oot3d_MaterialScalarRuntimeObjectPacketAddress(const u8* runtimeObject,
                                                   Oot3dMaterialScalarAddressResolver resolver,
                                                   void* resolverUser);
u8* Oot3d_MaterialScalarResolvePacketFromRuntimeObject(u32 runtimeObjectAddress,
                                                       Oot3dMaterialScalarAddressResolver resolver,
                                                       void* resolverUser);
int Oot3d_MaterialScalarBindRuntimeList(Oot3dFogMaterialSourceLayout* source,
                                         Oot3dFogMaterialRuntimeLayout* runtime,
                                         const u8* runtimeList,
                                         Oot3dMaterialScalarAddressResolver resolver,
                                         void* resolverUser,
                                         const Oot3dFogMaterialTemplateDecoded* templateData,
                                         u32 packedPicaFogLutAddress,
                                         Oot3dFogCurveTransform transform, void* transformUser);
int Oot3d_GameplayUpdateFogMaterialSource(Oot3dFogMaterialSourceLayout* source,
                                          Oot3dFogMaterialRuntimeLayout* runtime,
                                          u32 runtimeAddress,
                                          const float view[OOT3D_FOG_VIEW_WORD_COUNT],
                                          float nearZ, float farZ,
                                          u8 fogR, u8 fogG, u8 fogB, u8 fogA,
                                          u32* lutGenerationCounter,
                                          Oot3dFogCurveTransform transform, void* transformUser);
int Oot3d_GameplayApplyFogMaterialFrameOrder(const Oot3dGameplayFogMaterialFrameInput* input,
                                            Oot3dGameplayFogMaterialFrameResult* result);
int Oot3d_FogSourceBindMaterialState(Oot3dFogMaterialSourceLayout* source,
                                      Oot3dFogMaterialRuntimeLayout* runtime,
                                      const Oot3dFogMaterialTemplateDecoded* templateData,
                                      u8* materialPacket, u32 packedPicaFogLutAddress,
                                      Oot3dFogCurveTransform transform, void* transformUser);

#endif
