"""Symbolic-name table for `&NAME` references in .gcl source.

`&NAME` in .gcl text resolves to a 2-byte hashed-string literal (the
same encoding as `$s:XXXX`). This module is the curated mapping from
human-readable names to the 16-bit `GV_StrCode()` hashes the runtime
expects.

Entries below are seeded from [source/include/strcode.h] and the
existing [port/doc/gcl/08-known-ids.md] catalog. Add to this file as
you discover more meaningful names — every hash you map here also
becomes available as a `&NAME` token in source.

Precedence: when `gcx2gcl.py --names` decompiles, it picks the FIRST
entry registered for a given hash. To prefer one name over another,
register that one earlier in this file (or rename the existing).
"""

NAME_TO_HASH: dict[str, int] = {}
HASH_TO_NAME: dict[int, str] = {}


def _add(name: str, h: int) -> None:
    if name in NAME_TO_HASH:
        raise ValueError(f"duplicate name: {name}")
    NAME_TO_HASH[name] = h
    # First registration wins for the reverse table.
    HASH_TO_NAME.setdefault(h, name)


# ---------------------------------------------------------------------------
# Player & humanoid charas (CHARAID_*)
# ---------------------------------------------------------------------------
_add("SNAKE",         0x21CA)
_add("ZAKO",          0xED87)
_add("ZAKO10",        0x31E3)
_add("ZAKO14",        0x31E7)
_add("ZAKO19",        0x31EC)
_add("ZAKO11A",       0xA608)
_add("ZAKO11E",       0xA60C)
_add("ZAKO11F",       0xA60D)
_add("ZAKOCOM",       0x7CF7)
_add("ZK10COM",       0x8E64)
_add("ZK11ACOM",      0x5EFA)
_add("ZK11ECOM",      0x5F0A)
_add("ZK11FCOM",      0x5F0E)
_add("ZK14COM",       0x8E74)
_add("ZK19COM",       0x8E88)
_add("WATCHER",       0x6E9A)
_add("COMMANDER",     0xC6D7)
_add("NINJA",         0x30BA)
_add("NINJAPLAY",     0xB8D4)
_add("NINJA_D8DD",    0xD8DD)
_add("WOLF2",         0x962C)
_add("PSYCHOMANTIS",  0xA76F)
_add("PSYCHOMERYL",   0xF4B0)
_add("GODZILA",       0xCB1F)
_add("GODZCOM",       0x9EB7)
_add("LIQUID",        0x7BF2)
_add("JOHNNY",        0x1EF9)
_add("BLOODY_MERYL",  0x1158)
_add("MERYL3",        0xC755)
_add("MERYL7",        0x5078)
_add("MERYL72",       0xE271)
_add("MGREX",         0x4754)
_add("REVOLVER03",    0x050C)
_add("REVOLVER04",    0x05AF)
_add("OTACOM",        0xBF66)
_add("DOG",           0x6C0E)
_add("CROW",          0x8E60)
_add("ELE_CROW",      0x9AB9)

# ---------------------------------------------------------------------------
# Environment / stage props
# ---------------------------------------------------------------------------
_add("KAGE",          0x117C)
_add("SNOW",          0x18E3)
_add("SNOWAREA",      0x901E)
_add("SNOWSTORM",     0xA6F5)
_add("BUBBLE",        0x1A02)
_add("SMOKE",         0x170C)
_add("DSMOKE",        0x6A98)
_add("DSMOKE2",       0x76BC)
_add("B_SMOKE",       0x6B6C)
_add("DRUMCAN",       0xB58D)
_add("DRUMCAN2",      0x4BE8)
_add("DOOR",          0xB997)
_add("DOOR2",         0x73F8)
_add("M_DOOR",        0xB98C)
_add("LIFT",          0x425F)
_add("LIFT2",         0x921B)
_add("ELEVATOR",      0x2ABC)
_add("CHAIR",         0x788D)
_add("CONTAINER",     0xCC45)
_add("CRANE",         0xA3FB)
_add("GLASS",         0x8E70)
_add("WALL",          0xEC77)
_add("DMYFLOOR",      0x9D00)
_add("DMYWALL",       0x58F0)
_add("DYNWALL",       0xB103)
_add("DYNFLOOR",      0xAF6C)
_add("CAT_IN",        0x51C6)
_add("FURNACE",       0xADD8)
_add("PIPE",          0xC35F)
_add("ROPE",          0xBDA8)
_add("LAMP",          0x1AD3)
_add("PATO_LAMP",     0x30CE)
_add("PILOTLAMP",     0x169C)
_add("MIRROR",        0xC218)
_add("BED",           0x2A21)
_add("CAPE",          0xB99F)
_add("PAPER",         0x5F02)
_add("FOG",           0xD6FB)
_add("HIYOKO",        0x42E4)
_add("ASIATOKUN",     0x02C4)
_add("ASIOTOKUN",     0x92BC)
_add("WAKE",          0x41A3)
_add("RIPPLES",       0x63AA)

# ---------------------------------------------------------------------------
# Items / weapons / pickups
# ---------------------------------------------------------------------------
_add("ITEM",          0x8767)
_add("ITEM_DOT",      0x917B)
_add("KEY_ITEM",      0xC6AC)
_add("CLAYMORE",      0x3C0C)

# ---------------------------------------------------------------------------
# Camera / cinematic
# ---------------------------------------------------------------------------
_add("CAMERA",        0x6E90)
_add("CAMERA2",       0x56CC)
_add("CAMERA_SHAKE",  0x7BC2)
_add("DEMODOLL",      0xE97E)
_add("DEMOCANCEL",    0xB4E6)
_add("CINEMA",        0x7A05)
_add("DEMOSEL",       0x3686)
_add("MOVIE",         0x3453)
_add("FADEIO",        0xA12E)
_add("EMITTER",       0x32E5)
_add("EMITTER2",      0xA9DD)
_add("WALLSPARK",     0x2B24)
_add("BLOOD_CL",      0x4E95)
_add("BLOOD_BL",      0x6A4C)
_add("FALL_SPLASH",   0xC73E)
_add("SCN_BOMB",      0x600D)
_add("SEARCHLIGHT",   0xF50F)
_add("RADARPOINT",    0x5147)

# ---------------------------------------------------------------------------
# UI / menus / system
# ---------------------------------------------------------------------------
_add("MONITOR1",      0x6D78)
_add("DISPLAY",       0x9F7D)
_add("TELOP",         0x7FF7)
_add("PAUSE_MENU",    0xA5DC)
_add("SAVE_DATA",     0x9302)
_add("LOAD_DATA",     0x53C7)
_add("SAVEMANAGER",   0xC5B7)
_add("PADVIBRATE",    0xFED1)
_add("PADCONTROL",    0xCBF8)
_add("PADDEMO",       0x3ED7)
_add("PADDEMO2",      0x720D)
_add("COUNTDOWN",     0xECED)
_add("MENU",          0x226D)
_add("MOTIONSE",      0x0FAD)
_add("SPHERE",        0x73EA)
_add("SPHERE2",       0xBEE1)
_add("BREATH",        0x4170)

# ---------------------------------------------------------------------------
# Effects / atmospheric
# ---------------------------------------------------------------------------
_add("ENV_SOUND",     0x3F9A)
_add("ENV_TEST",      0x76FE)
_add("GAS_EFFECT",    0x5A50)
_add("GAS_DAMAGE",    0x8D5A)
_add("PLASMA",        0x9BC2)
_add("BLINK_TX",      0x8185)
_add("WATER",         0x96B5)
_add("SAFETY",        0xA2B5)

# ---------------------------------------------------------------------------
# Stage IDs (STAGE_*)
# ---------------------------------------------------------------------------
# Most-used stages — full list in source/include/strcode.h
_add("STAGE_init",    0x45CA)
_add("STAGE_title",   0x655B)
_add("STAGE_select",  0x8D5C)
_add("STAGE_rank",    0x9265)
_add("STAGE_brf",     0x96A7)
_add("STAGE_ending",  0x833B)
_add("STAGE_s00a",    0x469B)
_add("STAGE_s01a",    0x46BB)
_add("STAGE_s02a",    0x46DB)
_add("STAGE_s02b",    0x46DC)
_add("STAGE_s02c",    0x46DD)
_add("STAGE_s02d",    0x46DE)
_add("STAGE_s02e",    0x46DF)
_add("STAGE_s03a",    0x46FB)
_add("STAGE_s03b",    0x46FC)
_add("STAGE_s03c",    0x46FD)
_add("STAGE_s03d",    0x46FE)
_add("STAGE_s03e",    0x46FF)
_add("STAGE_s04a",    0x471B)
_add("STAGE_s04b",    0x471C)
_add("STAGE_s04c",    0x471D)
_add("STAGE_s05a",    0x473B)
_add("STAGE_s06a",    0x475B)
_add("STAGE_s07a",    0x477B)
_add("STAGE_s07b",    0x477C)
_add("STAGE_s07c",    0x477D)
_add("STAGE_s08a",    0x479B)
_add("STAGE_s08b",    0x479C)
_add("STAGE_s08c",    0x479D)
_add("STAGE_s09a",    0x47BB)
_add("STAGE_s10a",    0x4A9B)
_add("STAGE_s11a",    0x4ABB)
_add("STAGE_s11b",    0x4ABC)
_add("STAGE_s11c",    0x4ABD)
_add("STAGE_s11d",    0x4ABE)
_add("STAGE_s11e",    0x4ABF)
_add("STAGE_s11g",    0x4AC1)
_add("STAGE_s11h",    0x4AC2)
_add("STAGE_s11i",    0x4AC3)
_add("STAGE_s12a",    0x4ADB)
_add("STAGE_s12b",    0x4ADC)
_add("STAGE_s12c",    0x4ADD)
_add("STAGE_s13a",    0x4AFB)
_add("STAGE_s14e",    0x4B1F)
_add("STAGE_s15a",    0x4B3B)
_add("STAGE_s15b",    0x4B3C)
_add("STAGE_s15c",    0x4B3D)
_add("STAGE_s16a",    0x4B5B)
_add("STAGE_s16b",    0x4B5C)
_add("STAGE_s16c",    0x4B5D)
_add("STAGE_s16d",    0x4B5E)
_add("STAGE_s17a",    0x4B7B)
_add("STAGE_s18a",    0x4B9B)
_add("STAGE_s19a",    0x4BBB)
_add("STAGE_s19b",    0x4BBC)
_add("STAGE_s20a",    0x4E9B)
_add("STAGE_d00a",    0xC693)
_add("STAGE_d01a",    0xC6B3)
_add("STAGE_d03a",    0xC6F3)
_add("STAGE_d11c",    0xCAB5)
_add("STAGE_d16e",    0xCB57)
_add("STAGE_d18a",    0xCB93)

# ---------------------------------------------------------------------------
# HZD events / mesg payload codes (HASH_*)
# ---------------------------------------------------------------------------
_add("HASH_TRAP_ALL",  0x14C9)   # ？  (wildcard)
_add("HASH_ENTER",     0x0DD2)   # 入る
_add("HASH_LEAVE",     0xD5CC)   # 出る
_add("HASH_LEAVE2",    0x1A19)   # leave (alt)
_add("HASH_KILL",      0x3223)   # kill
_add("HASH_ON",        0x0E4E)   # on
_add("HASH_OFF",       0xC927)   # off
_add("HASH_ON2",       0xD182)   # ＯＮ
_add("HASH_OFF2",      0x006B)   # ＯＦＦ
_add("HASH_PADON",     0x2580)   # padon
_add("HASH_PADOFF",    0xAF6A)   # padoff
_add("HASH_SLOW",      0x3E92)   # slow
_add("HASH_SOUND_ON",  0x2761)   # 音入れる
_add("HASH_SOUND_OFF", 0xED7F)   # 音切る
_add("HASH_TABAKO",    0x8012)   # tabako
_add("HASH_POSITION",  0x62B6)   # position
_add("HASH_STOP",      0x5E8B)   # stop
_add("HASH_START",     0x9A1F)   # start
_add("HASH_STANCE",    0x3238)   # stance
_add("HASH_RUN_MOVE",  0x70FB)   # run_move
_add("HASH_MOTION",    0x937A)   # motion
_add("HASH_GO_MOTION", 0xBE0A)   # go_motion
_add("HASH_MOVE",      0x4B5D)   # move
_add("HASH_MOVE2",     0x89CB)   # 移動
_add("HASH_VOICE",     0x385E)   # voice
_add("HASH_TURN",      0xE2E9)   # turn
_add("HASH_MODE",      0x491D)   # mode
_add("HASH_LOOP",      0xCA87)   # loop

# ---------------------------------------------------------------------------
# Common map / area names
# ---------------------------------------------------------------------------
_add("HASH_MAIN",      0x7DF9)   # メイン  — main playable area
_add("HASH_ASIATO",    0xDC55)   # asiato
_add("HASH_POOL",      0xCA85)   # pool
_add("HASH_POOLATO",   0x7833)   # poolato

# ---------------------------------------------------------------------------
# Texture file IDs (PCX_*) — most-referenced
# ---------------------------------------------------------------------------
_add("PCX_LSIGHT",     0x08DB)
_add("PCX_SOCOM_F",    0xE4CC)
_add("PCX_SMOKE",      0x512D)
_add("PCX_MAGAZIN",    0x7E4C)
_add("PCX_BLOOD_2",    0x7B54)
_add("PCX_HEART",      0x1968)
_add("PCX_HOSI",       0xCAFE)
_add("PCX_ZZZ",        0xF7BB)
_add("PCX_DOOR2",      0x50EB)


def lookup(name: str) -> int | None:
    """Return the hash for `name` (ascii ident), or None if not registered."""
    return NAME_TO_HASH.get(name)


def name_for(h: int) -> str | None:
    """Return the canonical name for hash `h`, or None if unmapped."""
    return HASH_TO_NAME.get(h)
