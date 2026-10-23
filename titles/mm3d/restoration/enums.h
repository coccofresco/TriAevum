// SPDX-License-Identifier: GPL-2.0-or-later
// Ported from Project Restoration source/game headers (leoetlino/project-restoration 8bb2a7b8).
#pragma once
#include "rst_guest.h"
namespace Mm3dRestoration::Enum {
namespace Actor {
enum class Id : u16 {
  
  Player = 0,
  
  Arrow = 0xf,
  
  ObjElegyStatue = 0x1F,
  
  ClearTag = 0x73,
  
  ObjRailLift = 0xd8,
  
  BossOdolwa = 0xcb,
  
  BossTwinmold = 0xcc,
  
  BossGyorg = 0xcd,
  
  NpcKafei = 0x00F4,
  
  BgIcePlatform = 0x013E,
  
  BossGoht = 0x016E,
  
  ObjOwlStatue = 0x01B2,
  
  NpcOldLady = 0x01C5,
  
  NpcRosaSisters = 0x020A,
  
  NpcBombers = 0x020F,
  
  NpcHintStone = 0x0241,
  
  BgSeaFreezablePoint = 0x0244,
};
enum class Type : u8 {
  Switch = 0,
  Background = 1,
  Player = 2,
  Bomb = 3,
  Npc = 4,
  Enemy = 5,
  Prop = 6,
  Item = 7,
  Misc = 8,
  Boss = 9,
  Door = 10,
  Chest = 11,
};
enum class DamageType : u8 {
  Type1 = 1,
  FireArrow = 2,
  IceArrow = 3,
  Type4 = 4,
  Hookshot = 0xA,
  TypeB = 0xB,
  TypeC = 0xC,
  ArrowOrZoraBoomerang = 0xD,
  SwordOrBombOrBeamOrZora = 0xE,
  DekuNut = 0xF,
};
enum class Flag : u32 {
    Targetable = 0x1,
    TalkInitiated = 0x100,
  };
enum class Flag94 : u16 {
    Grounded = 1,
  };
}
namespace Player {
enum class FormParamIndex : u8 {
  FierceDeity = 0,
  Human = 1,
  Giant = 2,
  Deku = 3,
  Zora = 4,
  ZoraDiving = 5,
  Goron = 6,
  Unknown = 7,
};
enum class Form : u8 {
    FierceDeity = 0,
    Goron = 1,
    Zora = 2,
    Deku = 3,
    Human = 4,
  };
enum class ActionType : u8 {
    Type1 = 1,
    Type2 = 2,
    Type3 = 3,
    Type4 = 4,
    OcarinaOrTransformation = 5,
  };
enum class Flag1 : u32 {
    Locked = 0x1,
    Unk2 = 0x2,
    Unk4 = 0x4,
    Unk8 = 0x8,
    IsUsingZoraBarrier = 0x10,
    Unk20 = 0x20,
    Unk40 = 0x40,
    Dead = 0x80,
    Unk100 = 0x100,
    Unk200 = 0x200,
    Unk400 = 0x400,
    Unk800 = 0x800,
    ChargingSpinAttack = 0x1000,
    Unk2000 = 0x2000,
    Unk4000 = 0x4000,
    ZTargeting = 0x8000,
    ZTargetingWithTarget = 0x10000,
    ZTargetingWithoutTarget = 0x20000,
    InAir = 0x40000,
    Unk80000 = 0x80000,
    FirstPersonMode = 0x100000,
    Unk200000 = 0x200000,
    Shielding = 0x400000,
    Riding = 0x800000,
    LargeZoraFins = 0x1000000,
    ZoraFinsShot = 0x2000000,
    Unk4000000 = 0x4000000,
    InWater = 0x8000000,
    FreezeWorld = 0x10000000,
    FreezeLink = 0x20000000,
    Unk40000000 = 0x40000000,
    Unk80000000 = 0x80000000,
  };
enum class Flag2 : u32 {
    Moving = 0x20,
    
    Unk40 = 0x40,
    
    Held = 0x80,
    Underwater = 0x400,
    TatlFlying = 0x0010'0000,
    PlayingIdleAnimation = 0x1000'0000,
  };
enum class Flag3 : u32 {
    Unk1 = 0x1,
    Unk2 = 0x2,
    Unk4 = 0x4,
    AttackingB = 0x8,
    Unk10 = 0x10,
    Unk20 = 0x20,
    HasProjectileActor = 0x40,
    Unk80 = 0x80,
    DekuInFlower = 0x100,
    DekuLaunching = 0x200,
    DekuStuffMaybe = 0x400,
    Unk800 = 0x800,
    GoronRolling = 0x1000,
    DekuFlyingCamera = 0x2000,
    Unk4000 = 0x4000,
    ZoraFastSwimming = 0x8000,
    Unk10000 = 0x10000,
    AfterChangeMask = 0x20000,
    Unk40000 = 0x40000,
    GoronRollingFast = 0x80000,
    DekuSpin = 0x100000,
    Unk200000 = 0x200000,
    Unk400000 = 0x400000,
    AfterUseBoomerang = 0x800000,
    DekuNutsOnB = 0x1000000,
    Unk2000000 = 0x2000000,
    Unk4000000 = 0x4000000,
    Unk8000000 = 0x8000000,
    Unk10000000 = 0x10000000,
    Unk20000000 = 0x20000000,
    Unk40000000 = 0x40000000,
    Unk80000000 = 0x80000000,
  };
enum class ProjectileType : s16 {
    Arrow = 1,
    DekuBubble = 2,
    Hookshot = 3,
  };
enum class AllowExistingMagicUsage { No, Yes };
}
namespace Items {
enum class ItemId : u8 {
  Ocarina = 0x0,
  Arrow = 1,
  FireArrow = 2,
  IceArrow = 3,
  LightArrow = 4,
  FairyOcarina = 5,
  Bomb = 6,
  Bombchu = 7,
  DekuStick = 8,
  DekuNuts = 9,
  MagicBean = 0xa,
  FairySlingshot = 0xb,
  PowderKeg = 0xc,
  PictographBox = 0xd,
  LensOfTruth = 0xe,
  Hookshot = 0xf,
  GreatFairySword = 0x10,
  HookshotUnused = 0x11,

  Bottle = 0x12,
  RedPotion = 0x13,
  GreenPotion = 0x14,
  BluePotion = 0x15,
  Fairy = 0x16,
  DekuPrincess = 0x17,
  Milk = 0x18,
  MilkHalf = 0x19,
  Fish = 0x1a,
  Bug = 0x1b,
  BlueFire = 0x1c,
  Poe = 0x1d,
  BigPoe = 0x1e,
  Water = 0x1f,
  HotSpringWater = 0x20,
  ZoraEgg = 0x21,
  GoldDust = 0x22,
  MagicalMushroom = 0x23,
  SeaHorse = 0x24,
  ChateauRomani = 0x25,
  MysteryMilk = 0x26,
  MysteryMilkSpoiled = 0x27,

  MoonTear = 0x28,
  LandTitleDeed = 0x29,
  SwampTitleDeed = 0x2a,
  MountainTitleDeed = 0x2b,
  OceanTitleDeed = 0x2c,
  RoomKey = 0x2d,
  LetterToMama = 0x2e,
  LetterToKafei = 0x2f,
  PendantOfMemories = 0x30,

  MapUnused = 0x31,

  DekuMask = 0x32,
  GoronMask = 0x33,
  ZoraMask = 0x34,
  FierceDeityMask = 0x35,
  MaskOfTruth = 0x36,
  KafeiMask = 0x37,
  AllNightMask = 0x38,
  BunnyHood = 0x39,
  KeatonMask = 0x3a,
  GaroMask = 0x3b,
  RomaniMask = 0x3c,
  CircusLeaderMask = 0x3d,
  PostmanHat = 0x3e,
  CoupleMask = 0x3f,
  GreatFairyMask = 0x40,
  GibdoMask = 0x41,
  DonGeroMask = 0x42,
  KamaroMask = 0x43,
  CaptainHat = 0x44,
  StoneMask = 0x45,
  BremenMask = 0x46,
  BlastMask = 0x47,
  MaskOfScents = 0x48,
  GiantMask = 0x49,

  BowWithFireArrow = 0x4a,
  BowWithIceArrow = 0x4b,
  BowWithLightArrow = 0x4c,

  KokiriSword = 0x4d,
  RazorSword = 0x4e,
  GildedSword = 0x4f,

  FishingPass = 0x50,

  HeroShield = 0x51,
  MirrorShield = 0x52,

  Quiver = 0x53,
  LargeQuiver = 0x54,
  BiggestQuiver = 0x55,

  BombBag = 0x56,
  BigBombBag = 0x57,
  BiggestBombBag = 0x58,

  GreenWallet = 0x59,
  AdultWallet = 0x5a,
  GiantWallet = 0x5b,

  FishingRod = 0x5c,

  OdolwaRemains = 0x5d,
  GohtRemains = 0x5e,
  GyorgRemains = 0x5f,
  TwinmoldRemains = 0x60,

  SonataOfAwakening = 0x61,
  GoronLullaby = 0x62,
  NewWaveBossaNova = 0x63,
  ElegyOfEmptiness = 0x64,
  OathToOrder = 0x65,
  NocturneOfShadow = 0x66,
  SongOfTime = 0x67,
  SongOfHealing = 0x68,
  EponaSong = 0x69,
  SongOfSoaring = 0x6a,
  SongOfStorms = 0x6b,
  SunSong = 0x6c,

  BombersNotebook = 0x6d,

  GoldSkulltula = 0x6e,
  HeartContainer = 0x6f,
  PieceOfHeart = 0x70,

  X71 = 0x71,
  X72 = 0x72,
  X73 = 0x73,

  BossKey = 0x74,
  Compass = 0x75,
  Map = 0x76,
  StrayFairy = 0x77,
  SmallKey = 0x78,

  None = 0xff,
};
enum class Action : u8 {
  None = 0,
  Arrow = 0x9,
  FireArrow = 0xa,
  IceArrow = 0xb,
  LightArrow = 0xc,
  Hookshot = 0xd,
  DekuBubble = 0x12,
  PictographBox = 0x13,
  Ocarina = 0x14,
  GiantMask = 0x4F,
  FierceDeityMask = 0x50,
  GoronMask = 0x51,
  ZoraMask = 0x52,
  DekuMask = 0x53,
};
enum class MaskId : u8 {
  GiantMask = 0x14,
  FierceDeityMask = 0x15,
  GoronMask = 0x16,
  ZoraMask = 0x17,
  DekuMask = 0x18,
};
}
namespace Context {
enum class PauseFlag : u16 {
  PauseCalc = 1 << 0,
  PauseDraw = 1 << 1,
  PauseAll = PauseCalc | PauseDraw,
};
enum class OcarinaState : u16 {
  NotOpened = 0,
  Playing = 1,
  PlayingAndReplayDone = 3,
  StoppedPlaying = 4,
  PlayedSongOfTime = 0x12,
  PlayedInvertedSongOfTime = 0x13,
  GoingBackInTime = 0x16,
  RestoringTimeToNormalSpeed = 0x18,
  SlowingTime = 0x19,
  JumpingForwardInTime = 0x1a,
  WarpingToGreatBayCoast = 0x1c,
  WarpingToZoraCape = 0x1d,
  WarpingToSnowhead = 0x1e,
  WarpingToMountainVillage = 0x1f,
  WarpingToClockTown = 0x20,
  WarpingToMilkRoad = 0x21,
  WarpingToWoodfall = 0x22,
  WarpingToSouthernSwamp = 0x23,
  WarpingToIkanaCanyon = 0x24,
  WarpingToStoneTower = 0x25,
};
enum class OcarinaSong : u16 {
  SonataOfAwakening = 0,
  GoronLullaby = 1,
  NewWaveBossaNova = 2,
  ElegyOfEmptiness = 3,
  OathToOrder = 4,
  SongOfTime = 6,
  SongOfHealing = 7,
  EponaSong = 8,
  SongOfSoaring = 9,
  SongOfStorms = 10,
  InvertedSongOfTime = 12,
  SongOfDoubleTime = 13,

  InvalidDetecting = 0xfe,
  Invalid = 0xff,
};
}
namespace Camera {
enum class CameraState : u16 {
  NONE = 0x00,
  
  NORMAL0 = 0x01,
  
  NORMAL3 = 0x02,
  
  CIRCLE5 = 0x03,
  HORSE0 = 0x04,
  ZORA0 = 0x05,
  PREREND0 = 0x06,
  PREREND1 = 0x07,
  
  DOORC = 0x08,
  DEMO0 = 0x09,
  
  FREE0 = 0x0A,
  FUKAN0 = 0x0B,
  NORMAL1 = 0x0C,
  NANAME = 0x0D,
  
  CIRCLE0 = 0x0E,
  FIXED0 = 0x0F,
  SPIRAL = 0x10,
  
  DUNGEON0 = 0x11,
  ITEM0 = 0x12,
  
  ITEM1 = 0x13,
  ITEM2 = 0x14,
  ITEM3 = 0x15,
  
  NAVI = 0x16,
  WARP0 = 0x17,
  DEATH = 0x18,
  REBIRTH = 0x19,
  TREASURE = 0x1A,
  
  TRANSFORM = 0x1B,
  ATTENTION = 0x1C,
  WARP1 = 0x1D,
  DUNGEON1 = 0x1E,
  
  FIXED1 = 0x1F,
  FIXED2 = 0x20,
  MAZE = 0x21,
  REMOTEBOMB = 0x22,
  CIRCLE1 = 0x23,
  CIRCLE2 = 0x24,
  
  CIRCLE3 = 0x25,
  CIRCLE4 = 0x26,
  FIXED3 = 0x27,
  TOWER0 = 0x28,
  PARALLEL0 = 0x29,
  NORMALD = 0x2A,
  SUBJECTD = 0x2B,
  
  START0 = 0x2C,
  
  START2 = 0x2D,
  STOP0 = 0x2E,
  JCRUISING = 0x2F,
  CLIMEMAZE = 0x30,
  SIDED = 0x31,
  
  DUNGEON2 = 0x32,
  
  BOSS_SHIGE = 0x33,
  KEEPBACK = 0x34,
  CIRCLE6 = 0x35,
  CIRCLE7 = 0x36,
  CHUBOSS = 0x37,
  RFIXED1 = 0x38,
  TRESURE1 = 0x39,
  BOMBBASKET = 0x3A,
  CIRCLE8 = 0x3B,
  FUKAN1 = 0x3C,
  
  DUNGEON3 = 0x3D,
  TELESCOPE = 0x3E,
  
  ROOM0 = 0x3F,
  RCIRC0 = 0x40,
  CIRCLE9 = 0x41,
  ONTHEPOLE = 0x42,
  INBUSH = 0x43,
  BOSS_LAST = 0x44,
  BOSS_INI = 0x45,
  BOSS_HAK = 0x46,
  BOSS_KON = 0x47,
  CONNECT0 = 0x48,
  MORAY = 0x49,
  
  NORMAL2 = 0x4A,
  BOMBBOWL = 0x4B,
  CIRCLEa = 0x4C,
  WHIRLPOOL = 0x4D,
  KOKKOGAME = 0x4E,
  GIANT = 0x4F,
  
  SCENE0 = 0x50,
  ROOM1 = 0x51,
  
  WATER2 = 0x52,
  SOKONASI = 0x53,
  FORCEKEEP = 0x54,
  PARALLEL1 = 0x55,
  START1 = 0x56,
  ROOM2 = 0x57,
  NORMAL4 = 0x58,
  SHELL = 0x59,
  DUNGEON4 = 0x5A,
};
enum class CameraMode : u16 {
  NORMAL = 0x00,
  
  JUMP = 0x01,
  
  GORONDASH = 0x02,
  
  NUTSSHOT = 0x03,
  
  BOWARROWZ = 0x04,
  
  NUTSFLY = 0x05,
  
  SUBJECT = 0x06,
  
  BOOKEEPON = 0x07,
  
  ZORAFIN = 0x08,
  
  KEEPON = 0x09,
  
  PARALLEL = 0x0A,
  
  TALK = 0x0B,
  
  PACHINCO = 0x0C,
  BOWARROW = 0x0D,
  BATTLE = 0x0E,
  
  NUTSHIDE = 0x0F,
  
  STILL = 0x10,
  CHARGE = 0x11,
  
  CLIMB = 0x12,
  
  CLIMBZ = 0x13,
  FOOKSHOT = 0x14,
  
  FREEFALL = 0x15,
  HANG = 0x16,
  HANGZ = 0x17,
  PUSHPULL = 0x18,
  NUTSFLYZ = 0x19,
  
  GORONJUMP = 0x1A,
  BOOMERANG = 0x1B,
  CHARGEZ = 0x1C,
  ZORAFINZ = 0x1D,
  
  UNK_1E = 0x1E,
  
  FREECAMERA = 0x1F,
};
}
namespace Scene {
enum class SceneId : u16 {
  SouthernSwampClear = 0x00,
  TestMap01_01 = 0x01,
  TestMap01_02 = 0x02,
  TestMap01_03 = 0x03,
  TestMap01_04 = 0x04,
  TestMap01_05 = 0x05,
  TestMap01_06 = 0x06,
  Grottos = 0x07,
  Cutscenes = 0x08,
  Test01_09 = 0x09,
  PotionShop = 0x0A,
  MajoraLair = 0x0B,
  BeneathTheGraves = 0x0C,
  CuriosityShop = 0x0D,
  TestMap01_0E = 0x0E,
  TestMap02_0F = 0x0F,
  Barn = 0x10,
  HoneyAndDarling = 0x11,
  MayorResidence = 0x12,
  IkanaCanyon = 0x13,
  PirateFortress = 0x14,
  MilkBar = 0x15,
  StoneTowerTemple = 0x16,
  TreasureChestShop = 0x17,
  StoneTowerTempleInverted = 0x18,
  ClockTowerRooftop = 0x19,
  BeforeThePortalToTermina = 0x1A,
  WoodfallTemple = 0x1B,
  PathToMountainVillage = 0x1C,
  IkanaCastle = 0x1D,
  DekuScrubPlayground = 0x1E,
  OdolwaLair = 0x1F,
  ShootingGalleryTown = 0x20,
  SnowheadTemple = 0x21,
  MilkRoad = 0x22,
  PirateFortressInterior = 0x23,
  ShootingGallerySwamp = 0x24,
  PinnacleRock = 0x25,
  FairyFountain = 0x26,
  SwampSpiderHouse = 0x27,
  OceansideSpiderHouse = 0x28,
  AstralObservatory = 0x29,
  DekuTrial = 0x2A,
  DekuPalace = 0x2B,
  MountainSmithy = 0x2C,
  TerminaField = 0x2D,
  PostOffice = 0x2E,
  MarineResearchLaboratory = 0x2F,
  DampeHouse = 0x30,
  TestMap01_31 = 0x31,
  GoronShrine = 0x32,
  ZoraHall = 0x33,
  TradingPost = 0x34,
  RomaniRanch = 0x35,
  TwinmoldLair = 0x36,
  GreatBayCoast = 0x37,
  ZoraCape = 0x38,
  LotteryShop = 0x39,
  TestMap01_3A = 0x3A,
  PirateFortressExterior = 0x3B,
  FishermanHut = 0x3C,
  GoronShop = 0x3D,
  DekuKingChamber = 0x3E,
  GoronTrial = 0x3F,
  RoadToSouthernSwamp = 0x40,
  DoggyRacetrack = 0x41,
  CuccoShack = 0x42,
  IkanaGraveyard = 0x43,
  GohtLair = 0x44,
  SouthernSwamp = 0x45,
  Woodfall = 0x46,
  ZoraTrial = 0x47,
  GoronVillageSpring = 0x48,
  GreatBayTemple = 0x49,
  WaterfallRapids = 0x4A,
  BeneathTheWell = 0x4B,
  ZoraHallRooms = 0x4C,
  GoronVillageWinter = 0x4D,
  GoronGraveyard = 0x4E,
  SakonHideout = 0x4F,
  MountainVillageWinter = 0x50,
  GhostHut = 0x51,
  DekuShrine = 0x52,
  RoadToIkana = 0x53,
  SwordsmanSchool = 0x54,
  MusicBoxHouse = 0x55,
  IgosDuIkanaLair = 0x56,
  TouristInformation = 0x57,
  StoneTower = 0x58,
  StoneTowerInverted = 0x59,
  MountainVillageSpring = 0x5A,
  PathToSnowhead = 0x5B,
  Snowhead = 0x5C,
  PathToGoronVillageWinter = 0x5D,
  PathToGoronVillageSpring = 0x5E,
  GyorgLair = 0x5F,
  SecretShrine = 0x60,
  StockPotInn = 0x61,
  GreatBayCutscene = 0x62,
  ClockTowerInterior = 0x63,
  WoodsOfMystery = 0x64,
  LostWoods = 0x65,
  LinkTrial = 0x66,
  Moon = 0x67,
  BombShop = 0x68,
  GiantsChamber = 0x69,
  GormanTrack = 0x6A,
  GoronRacetrack = 0x6B,
  EastClockTown = 0x6C,
  WestClockTown = 0x6D,
  NorthClockTown = 0x6E,
  SouthClockTown = 0x6F,
  LaundryPool = 0x70,
  SwampFishingHole = 0x71,
  OceanFishingHole = 0x72,
  TerminaFieldCutscene1 = 0x73,
  TerminaFieldCutscene2 = 0x74,
};
}
namespace State {
enum class StateType : u8 {
  
  Initial = 0,
  
  
  FirstGame = 1,
  
  Dummy = 2,
  
  PrepareTitleScreen1 = 3,
  
  Play = 4,
  
  
  PrepareTitleScreen2 = 5,
  
  FileSelect = 6,
  
  DayTelop = 7,
  
  JokerHintMovie = 8,
  
  JokerEnding = 9,

  
  SotSave = 10,
};
enum class Status : u8 {
    
    
    Changing = 0,
    Initialising = 1,
    Running = 2,
  };
}
namespace Collision {
enum class AttackType : u32 {
  DekuNut = 0,
  DekuStick = 1,
  Epona = 2,
  Bomb = 3,
  ZoraFins = 4,
  Arrow = 5,
  Mirror = 6,
  Hookshot = 7,
  GoronPunch = 8,
  Sword = 9,
  GoronPound = 10,
  FireArrow = 11,
  IceArrow = 12,
  LightArrow = 13,
  GoronSpikes = 14,
  DekuSpin = 15,
  DekuBubble = 16,
  DekuFlower = 17,
  DekuNutDrop = 18,
  ZoraBarrier = 19,
  Type20 = 20,
  Type21 = 21,
  Type22 = 22,
  Zora = 23,
  GreatFairySword = 24,
  Beam = 25,
  Roll = 26,
  Type27 = 27,
  Type28 = 28,
  Type29 = 29,
  Type30 = 30,
  Keg = 31,
};
enum class Flag1 : u8 {
    Collided = 2,
  };
enum class Type : u8 {
    CylinderCollection = 0,
    Cylinder = 1,
    TriCollection = 2,
    Quad = 3,
    
    Type4 = 4,
  };
enum class DamageEffect : int {
  Damage = 0,
  NoDamage = 1,
  NoDamageYellow = 2,
};
}
namespace Sound {
enum class EffectId : u32 {
  NA_SE_EN_COMMON_FREEZE = 0x01000506,
  NA_SE_EN_FANTOM_DEAD = 0x0100074a,
  NA_SE_SY_TRE_BOX_APPEAR = 0x01000773,
  NA_SE_SY_DECIDE = 0x01000774,
  NA_SE_SY_QUEST_CLEAR = 0x010007dd,
  NA_SE_EN_MIBOSS_DAMAGE = 0x01000996,
  NA_SE_EN_MIBOSS_DEAD = 0x01000998,
  NA_SE_EN_MIBOSS_SWORD = 0x0100099d,
};
enum class StreamPlayer : u32 {
  DEFAULT_PLAYER = 0,
};
enum class StreamId : u32 {
  NA_BGM_BOSS00 = 0x0100001b,
};
}
}
