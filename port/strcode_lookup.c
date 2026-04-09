// This file provides a lookup table for known strcode values to their corresponding strings.
// Auto-generated from macros in strcode.h. Extend as needed.

#include <stdint.h>
#include <stddef.h>

struct strcode_entry {
    uint16_t code;
    const char *name;
};

static const struct strcode_entry strcode_lookup[] = {
    {0xea54, "scenerio"},
    {0xa242, "demo"},
    {0x0d86, "if"},
    {0x64c0, "eval"},
    {0xcd3a, "return"},
    {0x7636, "foreach"},
    {0x22ff, "mesg"},
    {0xd4cb, "trap"},
    {0x9906, "chara"},
    {0xc091, "map"},
    {0x7d50, "mapdef"},
    {0xeee9, "camera"},
    {0x306a, "light"},
    {0x9a1f, "start"},
    {0xc8bb, "load"},
    {0x24e1, "radio"},
    {0xe43c, "restart"},
    {0xa242, "demo"},
    {0xdbab, "ntrap"},
    {0x430d, "delay"},
    {0xcc85, "pad"},
    {0x5c9e, "varsave"},
    {0x4ad9, "system"},
    {0x698d, "sound"},
    {0x226d, "menu"},
    {0x925e, "rand"},
    {0xe257, "func"},
    {0xa2bf, "demodebug"},
    {0xb96e, "print"},
    {0xec9d, "jimaku"},
    {0x922b, "pan2"},
    {0x5d43, "item"},
    {0x14c9, "？"},
    {0x0dd2, "入る"},
    {0xd5cc, "出る"},
    {0x1a19, "leave"},
    {0x3223, "kill"},
    {0xc927, "off"},
    {0x006b, "ＯＦＦ"},
    {0x0e4e, "on"},
    {0xd182, "ＯＮ"},
    {0x2580, "padon"},
    {0xaf6a, "padoff"},
    {0x3e92, "slow"},
    {0x2761, "音入れる"},
    {0xed7f, "音切る"},
    {0x8012, "tabako"},
    {0x62b6, "position"},
    {0x5e8b, "stop"},
    {0x9a1f, "start"},
    {0x3238, "stance"},
    {0x70fb, "run_move"},
    {0x937a, "motion"},
    {0xbe0a, "go_motion"},
    {0x4b5d, "move"},
    {0x89cb, "移動"},
    {0x385e, "voice"},
    {0xe2e9, "turn"},
    {0x491d, "mode"},
    {0x4f34, "operation"},
    {0xf9ad, "マップ"},
    {0xca87, "loop"},
    {0x7df9, "メイン"},
    {0xdc55, "asiato"},
    {0xca85, "pool"},
    {0x7833, "poolato"},
    {0xb05c, "empty2"},
    {0x08db, "lsight"},
    {0xe4cc, "socom_f"},
    {0x512d, "smoke"},
    {0xdf92, "wt_sud11"},
    {0x7e4c, "magazin"},
    {0xfe8e, "fa_fl10"},
    {0xac92, "bomb1_fl"},
    {0x2447, "canon_seq"},
    {0x7b54, "blood_2"},
    {0x4cec, "sonic"},
    {0x55a6, "lense_flare1"},
    {0xdcd3, "b_mark"},
    {0xfad3, "q_mark"},
    {0x1968, "heart"},
    {0xf314, "pch_fog"},
    {0xcafe, "hosi"},
    {0xf7bb, "zzz"},
    {0xdc55, "asiato"},
    {0x479f, "rcm_l"},
    {0xa9cd, "w_bonbori"},
    {0x50eb, "door2"},
    {0xa0f4, "cd_warn"},
    // Add more entries as needed
};

static const size_t strcode_lookup_count = sizeof(strcode_lookup) / sizeof(strcode_lookup[0]);

// Returns the string for a given strcode, or NULL if not found
const char *strcode_to_string(uint16_t code) {
    for (size_t i = 0; i < strcode_lookup_count; ++i) {
        if (strcode_lookup[i].code == code) return strcode_lookup[i].name;
    }
    return NULL;
}
