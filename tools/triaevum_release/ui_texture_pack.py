"""The optional HD UI texture pack, obtained and prepared on the user's machine.

"OoT3D Vanilla UI/HUD" by chubbycubby is an Azahar texture pack: PNG redraws of
the HUD, menu and SingleScreen textures, keyed by the native textures' hashes,
with variants per game language. TriAevum never distributes it. On the user's
request Forge downloads the author's original archive from GameBanana, or copies
one the user selects, and keeps it only after its size and SHA-256 match the
revision recipe (the archive handling is romfs_mods.py's). It then extracts
exactly the catalogued textures and verifies each one; a folder the user
extracted with any tool is verified the same way.

The prepared pack sorts them for the runtime's --ui-texture-pack: the pack's
language folders in languages/<folder>/ and every other texture in common/.
The runtime loads, beneath the user's own texture pack, the folders of the
boot game language (the one chosen in Forge or in game), most specific first,
then common/ (UiTexturePackLanguageFolders in oot3d_game_language.h). One
texture hash may have language-specific art in several folders, so the order
decides; other languages' folders are never loaded.

The pack's HD SingleScreen UI atlas also serves TopScreen (topscreen_assets.py).
Its sys8.qbf is not a coverage font for the native atlas and is not used.

With the archive stored (beside Forge, under mods/ beside it, or in
data/downloads) "Prepare and install" applies the pack; without it the game
uses the native textures.
"""

from __future__ import annotations

import os
import shutil
import tempfile
import zipfile
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Mapping

try:
    from .common import atomic_write_json, load_json_object, sha256_file
    from .romfs_mods import ArchiveContract, Credit, archive_contract, stored_archive
except ImportError:
    from common import atomic_write_json, load_json_object, sha256_file
    from romfs_mods import ArchiveContract, Credit, archive_contract, stored_archive


SOURCE_RECEIPT = "source.json"
COMMON_DIRECTORY = "common"
LANGUAGES_DIRECTORY = "languages"
# The title's Azahar texture folder inside the archive (the USA title ID; the
# runtime matches textures by content hash, whatever the region).
TEXTURE_FOLDER = "load/textures/0004000000033500"
# The native menu language folders (rom:/menu/<folder>/) and the pack's
# per-language family folders.
LANGUAGE_FOLDERS = (
    "00_JP_JAPANESE", "01_US_ENGLISH", "02_EU_ENGLISH", "03_EU_GERMAN", "04_EU_FRENCH",
    "05_US_FRENCH", "06_EU_SPANISH", "07_US_SPANISH", "08_EU_ITALIAN",
    "GERMAN", "SPANISH", "ITALIAN", "JAPANESE",
)
UI_ATLAS_HD_TEXTURE = "UI/tex1_512x256_1FAF9B5B2ADF6038_13_mip0.png"


@dataclass(frozen=True)
class UiTexturePack:
    """A catalogued texture pack: identity, credits, licence and its textures.

    Members are paths below TEXTURE_FOLDER inside the archive's root folder."""
    mod_id: str
    archive_kind: str
    title: str
    version: str
    author: str
    page: str
    downloads: tuple[str, ...]
    license_name: str
    license_url: str
    credits: tuple[Credit, ...]
    archive_root: str
    members: Mapping[str, tuple[int, str]]

    @property
    def attribution(self) -> str:
        return f"{self.title} {self.version} by {self.author} ({self.page}), {self.license_name}"


def prepared_path(member: str) -> PurePosixPath:
    """Where a member goes in the prepared pack: a language folder's texture in
    languages/<folder>/, any other in common/ (keeping its path)."""
    parts = PurePosixPath(member).parts
    if len(parts) == 3 and parts[0] == "UI" and parts[1] in LANGUAGE_FOLDERS:
        return PurePosixPath(LANGUAGES_DIRECTORY, parts[1], parts[2])
    return PurePosixPath(COMMON_DIRECTORY, *parts)


# --- OoT3D Vanilla UI/HUD 3.2 (chubbycubby) -------------------------------------
# GameBanana's file link for oot3d_vanilla_ui_hud_32.zip; it redirects to the
# file host.
VANILLA_UI_HUD_DOWNLOAD = "https://gamebanana.com/dl/1857740"

VANILLA_UI_HUD = UiTexturePack(
    mod_id="vanilla_ui_hud",
    archive_kind="vanilla_ui_hud_3_2_archive",
    title="OoT3D Vanilla UI/HUD",
    version="3.2",
    author="chubbycubby",
    page="https://gamebanana.com/mods/701929",
    downloads=(VANILLA_UI_HUD_DOWNLOAD,),
    license_name="CC BY-NC-ND 4.0",
    license_url="https://creativecommons.org/licenses/by-nc-nd/4.0/",
    credits=(
        # The submitter, then the mod page's credits.
        Credit("Submitter", "chubbycubby", "", "https://gamebanana.com/members/5093056"),
        Credit("Key Authors", "HenrikoMagnifico", "", "https://gamebanana.com/members/1655768"),
        Credit("Key Authors", "M-1", "", "https://gamebanana.com/members/1544353"),
    ),
    archive_root="OoT3D Vanilla UI⁄HUD 3.2",
    members={
        "BOSH RUSH/tex1_1024x256_20E7AFFF1BE07C10_12_mip0.png": (2339780, "8a414aaca7beffad9cfa384b24a36cc88106452eafac8e7c81ec3ff0c6540311"),
        "BOSH RUSH/tex1_1024x256_3B8E791D26BB81DA_12_mip0.png": (2156321, "b0ef705720fdaa399b1737cad9cbc68236c99e82287760079f7a50e303fde5fb"),
        "BOSH RUSH/tex1_1024x256_53A162747ACB2F03_12_mip0.png": (1778375, "ae37197bec05e38bd7ded0d89bf9963afcbe3d2166716cfea47dda35e6f51d08"),
        "BOSH RUSH/tex1_1024x256_628A4CA2591B91A6_12_mip0.png": (2157460, "394dc211c94b8e6792dbd25da647e732870d57d0811d2670fd08582b8d995bbb"),
        "BOSH RUSH/tex1_1024x256_B8344F33CDDB8882_12_mip0.png": (2395373, "6f8991236aff00b8030efda39ec6775dfc30485b2e7fe64275136d5de25715e9"),
        "BOSH RUSH/tex1_1024x256_CB1702F468844A59_12_mip0.png": (2211069, "573ffe8fbb42f87e95cbdba455ba81f804d739e3824998dd6e081d6e9e304f16"),
        "BOSH RUSH/tex1_1024x256_EF9BBB620180A6B2_12_mip0.png": (2089038, "f4d846fc98a09e762fcc0e6d84d781738839756b67ed52bba90017a916884a87"),
        "BOSH RUSH/tex1_1024x256_F010ABC3EF3BF6A1_12_mip0.png": (2454796, "4e2860a3d454be87f7f9f44911a0adebeaf200eeca511806e2dbece29096718b"),
        "LINK/tex1_128x32_33AEEEAC5F3D23D8_12_mip0.png": (576022, "28f8a303c0fc0505ff1f3b047297ffa04df43355c798a109ded55680d6325b29"),
        "UI/00_JP_JAPANESE/tex1_128x128_48D02A87589F37FA_4_mip0.png": (729814, "2c58bf6e86ccc89ffcc7c117ae51bbc6c77f9ee99096ac98f8c887543aeec80c"),
        "UI/00_JP_JAPANESE/tex1_512x256_A2D74F45889F712A_4_mip0.png": (9290645, "e01f4846761e607b40c450afa847f5a75b8f8e82967a56a11ccf66602429b10f"),
        "UI/01_US_ENGLISH/tex1_128x128_1BDBC2AFBE4A4379_4_mip0.png": (729814, "2c58bf6e86ccc89ffcc7c117ae51bbc6c77f9ee99096ac98f8c887543aeec80c"),
        "UI/01_US_ENGLISH/tex1_512x256_BB00B25B152582B6_4_mip0.png": (9231712, "c829aad9b025be81717a0d2c279146c4dce814cfb6a2c56bed0d8db0e4e6f6b5"),
        "UI/02_EU_ENGLISH/tex1_512x256_164411C4E5F37729_4_mip0.png": (9233095, "5d5e88e94aee770965d45a16f7beda8e7d815c9ea778a2bba473f857975dfb7d"),
        "UI/03_EU_GERMAN/tex1_512x256_935CB74571E453B5_4_mip0.png": (9213778, "61db2b91ee397d35872abdf2ff00b400f48fe2b91d0f050d3e70e2b57b4932dd"),
        "UI/04_EU_FRENCH/tex1_512x256_143E9DF8CA80C390_4_mip0.png": (9210746, "ebdcb67756ecefb0fda5e54a17b6ec2196a5230c8955eedf5a743fe01e513b60"),
        "UI/05_US_FRENCH/tex1_512x256_F43D6593C5248F84_4_mip0.png": (9211208, "ef884633e50c7057b0723e74f42d9e5e56325da832d02d7171e6746472ebc05a"),
        "UI/06_EU_SPANISH/tex1_512x256_6664660F78E9D8C4_4_mip0.png": (9486604, "644651290dd80027b084470e511f5c300de25d54c04b8c3f398ddf51c2cff512"),
        "UI/07_US_SPANISH/tex1_512x256_9623362A9111CEDA_4_mip0.png": (9236892, "eafd2746f53b9d640e0660f7e8fb4ddf352286a6bfd59a15618d8d9f9e3df89b"),
        "UI/08_EU_ITALIAN/tex1_512x256_1866B83D16C1D4AD_4_mip0.png": (9229101, "49a08635faa2c7fab493f73493aff66ed40d19f069d3902826fe4ef3a948fa0a"),
        "UI/GERMAN/tex1_256x256_BD79A583E6B3184C_13_mip0.png": (16876335, "4fdb5906083bf66028663b3f77c696948665723971c97eeb9b2fb18fb606a686"),
        "UI/GERMAN/tex1_256x256_BEFF68CBB3F5DB7A_4_mip0.png": (2290200, "9f54fa5d84a80e2248d181aebd03fa315c6b1a5683104f572d10fddc2bad874c"),
        "UI/GERMAN/tex1_512x256_00574E9DFD8D9DA3_4_mip0.png": (9079066, "7116480dae28d8903ed9b0bc39824a27f5281dfdc2638b13109974e71a69f98e"),
        "UI/GERMAN/tex1_512x256_005ABBB7ED9A78C5_13_mip0.png": (5283773, "4f173c3f427369bf07bd7aab622085c9266ec011361ec630a0b0ec977e09a68d"),
        "UI/GERMAN/tex1_512x256_1A0DFC081C7089F6_13_mip0.png": (10752016, "08adcb8407e5758096f9038b14e5f6b8b84566a79940b37a2558752467a3f52c"),
        "UI/GERMAN/tex1_512x256_86CB4EEC9E71A595_4_mip0.png": (6035340, "55ebad87bff3c47b09772ea3dfde74f720013580c1070899efa688ecbc695bea"),
        "UI/ITALIAN/tex1_256x256_130115FE45E60748_4_mip0.png": (2245341, "a4e49d33dae63770017e2daa8ba8727ffa87ac9cf309312218c565e915619cb6"),
        "UI/ITALIAN/tex1_256x256_88EC1FC5270E6D48_13_mip0.png": (16752952, "697224333537724995240575c03a7aaae81a83a2bfb6933663fe3a1ffdeeae4d"),
        "UI/ITALIAN/tex1_512x256_7DECB6B3C2EB8C22_13_mip0.png": (5267775, "b266c4ecc5206b0cfb51d3078bdec7b2e8f323667560b7aed4c44f2eea00fd17"),
        "UI/ITALIAN/tex1_512x256_8FE4BEB04BC6FBFE_4_mip0.png": (9074753, "a823878f4cb642e3738ad13c8054e36da820b02139279a9de1402639fec2ec79"),
        "UI/ITALIAN/tex1_512x256_BFA9AE5AA44DC03D_4_mip0.png": (6089320, "309c1770dbf543bb0764fef8cba5ed336d75b9537c026da2a05adb42a5e67e83"),
        "UI/ITALIAN/tex1_512x256_C0E228F420E580EA_13_mip0.png": (10725907, "5e96e660deea0e73f4ec1bcd1eda3522626b6528e668c2aefde299e338bebb1c"),
        "UI/JAPANESE/tex1_512x512_608FDA1F6B644EA8_13_mip0.png": (12491658, "9ccc1e5484ae7334bbedf74cab5f99c23ac1eba019bfe54b01a999e11263fd99"),
        "UI/SPANISH/tex1_256x256_E8B0409A55AFE44A_4_mip0.png": (2497366, "44ac14d0d65816f0afa18be1f8d3e18aa22c207a605115b1c1dc19070f143309"),
        "UI/SPANISH/tex1_256x256_F7B7466A6CFCA03C_13_mip0.png": (18193965, "177d8e4e87c7bec706604161bbd9d073e56a73a4ac8e61ce8aa2734f11f69729"),
        "UI/SPANISH/tex1_512x256_16FF30A5E8A006CD_13_mip0.png": (5302675, "26e6557935836eca4e8afa4350d5592a565a3bf67c21012e53631329a2c5a7c2"),
        "UI/SPANISH/tex1_512x256_642C9A3840C1E75D_13_mip0.png": (10869374, "632dd74b438c6cce63316dd7c8feb39781199e72e7762db958a0c7f3d46c8588"),
        "UI/SPANISH/tex1_512x256_9623362A9111CEDA_4_mip0.png": (9297723, "ac55e471de69765259f6f50b161cb05074737d9656dbd2915647c2433a858f3d"),
        "UI/SPANISH/tex1_512x256_A008B07946EFA07B_13_mip0.png": (7450118, "a6b8488751225ec03ac8ff2ec138671918116c5f1e8f040cc9c179a6e45578c4"),
        "UI/SPANISH/tex1_512x256_D351AEB174751DE5_4_mip0.png": (6100320, "9f91317951fdbf04ca486abbbcd35438289d8ba155f5c1b5c6dbc23bfd382b61"),
        "UI/tex1_1024x256_EF6DE408B40738C1_5_mip0.png": (53137, "1bedc4b3d3cc6815e97efa6bd7dea60f98acf212fa9a72c2c75d99abe6f3e631"),
        "UI/tex1_128x128_0B70FB658193F026_4_mip0.png": (729743, "45b1d0be4a22a507e474e897f26ef400200adf644fd259e6b237777f9808b0c8"),
        "UI/tex1_128x128_1BDBC2AFBE4A4379_4_mip0.png": (729743, "45b1d0be4a22a507e474e897f26ef400200adf644fd259e6b237777f9808b0c8"),
        "UI/tex1_128x128_4F3728D6623696E3_4_mip0.png": (546724, "673b806aaea6fcfacc039649aeb6fad6c06be5ce40b85415ca93832d3157b67c"),
        "UI/tex1_128x128_6C9C3ED9D52C3754_4_mip0.png": (729804, "a4c8fadb3daf9fb7ee791671d94bc909af9e791599634a768c8b28ad3dea5e4a"),
        "UI/tex1_128x128_8064FDFB5065B51D_4_mip0.png": (546724, "673b806aaea6fcfacc039649aeb6fad6c06be5ce40b85415ca93832d3157b67c"),
        "UI/tex1_128x32_3149F395AE28A5F7_5_mip0.png": (32904, "5a821da3859ba957162b39f579c4c4c015fac4e6cba99f066056db198c7c3160"),
        "UI/tex1_128x32_EE6E53B64A496595_13_mip0.png": (393873, "2525dc600bb479f159d20f9517c763ce0ef227e94051c3aa61369bccacb74754"),
        "UI/tex1_128x64_110C74C9F44174CF_4_mip0.png": (195306, "1264d73e020b6dd81b34ee964fc241b6730c804e90f9f27f0c9817edd96f2772"),
        "UI/tex1_256x128_2076665BB31A80EC_4_mip0.png": (1281923, "088942f04033a51e6cdb256333e209306d9c7841fd08333444a324fd2264d851"),
        "UI/tex1_256x128_D352C1053F32C83C_4_mip0.png": (1281923, "088942f04033a51e6cdb256333e209306d9c7841fd08333444a324fd2264d851"),
        "UI/tex1_256x128_FABD759BB95C1C6A_4_mip0.png": (159598, "9406bd7bd024bf01702c27ec9b2aaae320ad6f67fe3d009c9cc745641edd9018"),
        "UI/tex1_256x256_18DDBB03B81C1D60_13_mip0.png": (16587614, "e9fb076c31d917968b1db88e1e624c40b4c055599ec410b37fdb48517d6c0368"),
        "UI/tex1_256x256_554DABA69F3B3E2C_4_mip0.png": (2522490, "1e40e02ffb88b0759b983c37cb319119960111c52797064359f96f62c02baf94"),
        "UI/tex1_256x256_B643C054F4538362_4_mip0.png": (2522537, "12e7681454ed569dc62e8afac6c982db7f50cbd897ce898594c98b6f548ca18e"),
        "UI/tex1_256x256_D6B04AFDACA985EF_13_mip0.png": (2957251, "772563ec6a8dbe3489d1992d24edccd5176603ab9585126e5bcb8ca89308039a"),
        "UI/tex1_256x32_63699458070CD2D1_4_mip0.png": (125092, "94ca6e9a6c66a38fbf8a15f9b81907ba44378631bb4ba2d5de1e2343cd442529"),
        "UI/tex1_256x32_A8F95F0A04D3DCCF_4_mip0.png": (155996, "d3f2c327f9d8db254d5ecf94da387ab94f6284531109efa1a9452b9c2d8e4ca8"),
        "UI/tex1_256x64_6D1358119030B3B9_5_mip0.png": (20309, "384dc3ec4bbcbc28ff52f745b9cda8e37cf231b4cde14eaa1fff9969b6b4996d"),
        "UI/tex1_512x256_164411C4E5F37729_4_mip0.png": (9232234, "85c21433c7f862f86d874235d324c66790f45ec15d25741e4246eb61c7c75950"),
        "UI/tex1_512x256_1B1E40F8F6E53947_13_mip0.png": (5469793, "a1759f3536ccf3d5d8dc7395749854378814ca44a8a2873defdc88db0bc95344"),
        "UI/tex1_512x256_1FAF9B5B2ADF6038_13_mip0.png": (7165558, "78b23fe8fe73d480e9386fd06bb365e038a1a8ffefe910ceaf579997e5b85b0d"),
        "UI/tex1_512x256_439913BD09FA2671_13_mip0.png": (7388994, "775c2278f7964365d35bb7b0d7820553c15393143946ea9ead0e406c68813a88"),
        "UI/tex1_512x256_71EC8EF34C17BF8F_4_mip0.png": (7099290, "89518a454c5c05d7c7483ef14cda7ba990c87db1e5ff0e2b246a2a3164132fa0"),
        "UI/tex1_512x256_7D6716CEB0D7F7FA_4_mip0.png": (9240163, "8386943afb2b66e17b7b0b7f228e4013007d27c695ae4794a99731d60a137770"),
        "UI/tex1_512x256_8E797095DBD2BD5F_13_mip0.png": (10627376, "4054599a5a546175ac89e5da5b86e0df00a9b9e6b1e334d9ba414fa38b953f2e"),
        "UI/tex1_512x256_BB00B25B152582B6_4_mip0.png": (9231680, "c0c4daa92c6052ac1f2bcc53b975604c940c688b13055bea9b0c9fe1d4c4d3f7"),
        "UI/tex1_512x256_C0F1BD5D88603724_4_mip0.png": (7099290, "89518a454c5c05d7c7483ef14cda7ba990c87db1e5ff0e2b246a2a3164132fa0"),
        "UI/tex1_512x256_CC7BF161FC89CB3B_13_mip0.png": (5675095, "d50e60b13d4aaf96790abe6621a088cb362b70232d5629cd0c0618437cd7b850"),
        "UI/tex1_512x256_EF60A5ECFCD8F99D_13_mip0.png": (10757323, "c8e6210a71cf53e3af6738eddbafa4dfc90d8eef2151430fa8f258cbd448ad97"),
        "UI/tex1_512x256_F8DA04382CECA85C_4_mip0.png": (6557507, "b6f72dc8fdcae94e20ac8c3e953b22d7c3827ca6e8de1faefac5970f074d44d6"),
        "UI/tex1_512x512_792B78634E49D46D_4_mip0.png": (20806569, "8d16d246053b5bf122f0aeb80bc21a384a88b70fea6b9f553960e6f59fb9494e"),
        "UI/tex1_512x512_962F45C78450C392_0_mip0.png": (11172188, "0d43357ccc7f2deb16b0bbe978503bdecda80f6440e7ee453dec6acb3ed265fe"),
        "UI/tex1_512x512_CF461E58E637A97A_13_mip0.png": (12491658, "9ccc1e5484ae7334bbedf74cab5f99c23ac1eba019bfe54b01a999e11263fd99"),
        "UI/tex1_512x512_F8B2C222A11AD460_4_mip0.png": (8272611, "cc25b7429656909917e07308844a9899b805080a39259b784d6701fd9d09d9ef"),
        "UI/tex1_64x64_A3C950DFBD94A309_4_mip0.png": (32587, "c6904249bf81dfa09679d0cf25ed0531eab416b5d876e3734719bed001c0e94f"),
    },
)


def pack_contract(recipes: Path | dict, pack: UiTexturePack = VANILLA_UI_HUD) -> ArchiveContract | None:
    """The archive the recipes (a recipes file, or one recipe) accept, or None."""
    return archive_contract(recipes, pack)


def _member_ok(path: Path, expected: tuple[int, str]) -> bool:
    return (path.is_file() and not path.is_symlink() and path.stat().st_size == expected[0]
            and sha256_file(path) == expected[1])


def pack_directory(data_root: Path, pack: UiTexturePack = VANILLA_UI_HUD) -> Path:
    return data_root / "mods" / "ui-textures" / f"{pack.mod_id}-{pack.version}"


def prepared_pack(data_root: Path, pack: UiTexturePack = VANILLA_UI_HUD) -> Path | None:
    """The verified prepared pack in the private data folder, or None."""
    directory = pack_directory(data_root, pack)
    receipt = directory / SOURCE_RECEIPT
    if not receipt.is_file():
        return None
    try:
        payload = load_json_object(receipt)
    except (OSError, ValueError):
        return None
    if payload.get("mod") != pack.mod_id or payload.get("version") != pack.version:
        return None
    if not all(_member_ok(directory / prepared_path(name), expected)
               for name, expected in pack.members.items()):
        return None
    return directory


def ui_atlas_hd(directory: Path | None) -> Path | None:
    """The pack's HD SingleScreen UI atlas in a prepared pack."""
    if directory is None:
        return None
    candidate = directory / prepared_path(UI_ATLAS_HD_TEXTURE)
    return candidate if candidate.is_file() else None


def _publish(staging: Path, data_root: Path, pack: UiTexturePack, origin: str) -> Path:
    for name, expected in pack.members.items():
        if not _member_ok(staging / prepared_path(name), expected):
            raise ValueError(f"{pack.title}: {name} is missing or differs from version {pack.version}")
    atomic_write_json(staging / SOURCE_RECEIPT, {
        "mod": pack.mod_id, "version": pack.version, "origin": origin,
        "attribution": pack.attribution, "license": pack.license_name,
        "license_url": pack.license_url, "source_url": pack.page,
        "private_local_artifact": True, "redistributable": False})
    destination = pack_directory(data_root, pack)
    if destination.exists():
        shutil.rmtree(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    os.replace(staging, destination)
    prepared = prepared_pack(data_root, pack)
    if prepared is None:
        raise ValueError(f"{pack.title}: the extracted files changed while they were stored")
    return prepared


def _staging(data_root: Path, pack: UiTexturePack, prefix: str) -> Path:
    parent = pack_directory(data_root, pack).parent
    parent.mkdir(parents=True, exist_ok=True)
    return Path(tempfile.mkdtemp(prefix=prefix, dir=parent))


def extract_archive(archive: Path, data_root: Path, contract: ArchiveContract,
                    pack: UiTexturePack = VANILLA_UI_HUD) -> Path:
    """Extract exactly the catalogued textures of the verified original archive."""
    if not (archive.is_file() and not archive.is_symlink() and archive.stat().st_size == contract.bytes
            and sha256_file(archive) == contract.sha256):
        raise ValueError(f"{pack.title} archive failed verification: {archive}")
    staging = _staging(data_root, pack, ".extract-")
    try:
        with zipfile.ZipFile(archive) as source:
            for name, (size, _digest) in pack.members.items():
                entry = f"{pack.archive_root}/{TEXTURE_FOLDER}/{name}"
                try:
                    info = source.getinfo(entry)
                except KeyError:
                    raise ValueError(f"The {pack.title} archive has no {name}") from None
                if info.file_size != size:
                    raise ValueError(f"{name} differs from {pack.title} {pack.version}")
                target = staging / prepared_path(name)
                target.parent.mkdir(parents=True, exist_ok=True)
                with source.open(info) as payload, target.open("wb") as output:
                    shutil.copyfileobj(payload, output, 4 * 1024 * 1024)
        return _publish(staging, data_root, pack, f"archive:{contract.sha256}")
    finally:
        shutil.rmtree(staging, ignore_errors=True)


def import_extracted(folder: Path, data_root: Path, pack: UiTexturePack = VANILLA_UI_HUD) -> Path:
    """Verify and keep the textures of a folder the user extracted with any
    tool: the pack's folder, its load/ folder, or the title texture folder."""
    folder = folder.expanduser().resolve()
    title_folder = PurePosixPath(TEXTURE_FOLDER)
    candidates = [folder, folder / title_folder.relative_to("load"), folder / title_folder.name,
                  folder / title_folder]
    if folder.is_dir():
        candidates += [child / title_folder for child in sorted(folder.iterdir()) if child.is_dir()]
    root = next((item for item in candidates if all((item / name).is_file() for name in pack.members)), None)
    if root is None:
        raise ValueError(f"This folder does not contain the extracted {pack.title} {pack.version} textures "
                         f"(expected {TEXTURE_FOLDER}/{UI_ATLAS_HD_TEXTURE} and the others)")
    staging = _staging(data_root, pack, ".import-")
    try:
        for name, expected in pack.members.items():
            source = root / name
            if source.is_symlink() or not _member_ok(source, expected):
                raise ValueError(f"{name} differs from {pack.title} {pack.version}")
            target = staging / prepared_path(name)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)
        return _publish(staging, data_root, pack, "extracted_folder")
    finally:
        shutil.rmtree(staging, ignore_errors=True)


def pack_status(root: Path, data_root: Path, contract: ArchiveContract | None,
                pack: UiTexturePack = VANILLA_UI_HUD) -> str:
    """'ready' (verified textures prepared), 'archive' (archive stored, not
    yet prepared) or 'missing'."""
    if prepared_pack(data_root, pack) is not None:
        return "ready"
    if contract is not None and stored_archive(root, data_root, contract) is not None:
        return "archive"
    return "missing"


def prepare_ui_texture_pack(*, root: Path, data_root: Path, recipe: dict,
                            report=lambda *_: None, pack: UiTexturePack = VANILLA_UI_HUD) -> Path | None:
    """The prepared pack, or None when the revision does not accept it or the
    user has not obtained it. A stored archive that fails is reported and left
    out: the title is still prepared, with the native textures."""
    contract = pack_contract(recipe, pack)
    if contract is None:
        return None
    prepared = prepared_pack(data_root, pack)
    if prepared is not None:
        return prepared
    archive = stored_archive(root, data_root, contract)
    if archive is None:
        return None
    report("ui_textures", f"Preparing {pack.title} {pack.version} from the original archive...")
    try:
        return extract_archive(archive, data_root, contract, pack)
    except (OSError, ValueError, zipfile.BadZipFile) as exc:
        report("ui_textures", f"{exc} The game uses the native UI textures.")
        return None
