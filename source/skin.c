#include "skin.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

const char *SLOT_NAME[SC_COUNT] = { "Skin", "Hair", "Eyes", "Top", "Bottom", "Shoes", "Hat", "Pack" };
const char *PIECE_CAT[SP_COUNT] = { "Top", "Bottom", "Shoes", "Hat", "Face" };
const int PIECE_COUNT[SP_COUNT] = { 4, 4, 3, 4, 3 };
const char *PIECE_NAME[SP_COUNT][4] = {
    { "T-shirt", "Oversized tee", "Hoodie", "Puffer jacket" },
    { "Shorts", "Baggy jeans", "Cargo pants", "Joggers" },
    { "Hiking boots", "Chunky sneakers", "High-tops", "" },
    { "None", "Beanie", "Bucket hat", "Cap" },
    { "Round eyes", "Sparkle eyes", "Sleepy eyes", "" },
};

static void set(Skin *s, int slot, uint32_t hex) { s->rgb[slot][0] = hex >> 16; s->rgb[slot][1] = hex >> 8; s->rgb[slot][2] = hex; }

void skin_default(Skin *s) { skin_preset(s, 0); }

void skin_preset(Skin *s, int i) {
    static const uint32_t P[SKIN_PRESETS][SC_COUNT] = {
        //  skin     hair     eyes     top      bottom   shoes    hat      pack
        { 0x7FE3C4, 0x8F5CDB, 0x2B1E3A, 0xF97A40, 0x2F9FB0, 0x614238, 0xF2D13A, 0xEB4759 },
        { 0xFF8F7A, 0x1B1B1B, 0x3A2A1A, 0x3FD18A, 0x5C6C8C, 0xF4F4F4, 0x3FD18A, 0x2B64D9 },
        { 0xC9A8FF, 0xF06FA8, 0x1C3A7A, 0xB48CFF, 0x1D1D2B, 0xFF5E5E, 0x1D1D2B, 0xFFD23F },
        { 0xFFE27A, 0xE8E2D0, 0x1F5F4A, 0x2D2D35, 0x8A8F99, 0x3FA7FF, 0x3FA7FF, 0x6BCB77 },
    };
    memset(s, 0, sizeof *s);
    for (int k = 0; k < SC_COUNT; k++) set(s, k, P[i % SKIN_PRESETS][k]);
    static const uint8_t pcs[SKIN_PRESETS][SP_COUNT] = { { 0, 0, 0, 0, 0 }, { 2, 1, 1, 1, 1 }, { 1, 3, 1, 2, 2 }, { 3, 2, 2, 3, 0 } };
    memcpy(s->piece, pcs[i % SKIN_PRESETS], SP_COUNT);
}

void skin_pack(const Skin *s, uint8_t out[SKIN_BYTES]) {
    out[0] = 1;                                   // format version
    memcpy(out + 1, s->rgb, SC_COUNT * 3);
    memcpy(out + 1 + SC_COUNT * 3, s->piece, SP_COUNT);
}

bool skin_unpack(Skin *s, const uint8_t in[SKIN_BYTES]) {
    if (in[0] != 1) return false;
    Skin t;
    memcpy(t.rgb, in + 1, SC_COUNT * 3);
    memcpy(t.piece, in + 1 + SC_COUNT * 3, SP_COUNT);
    for (int p = 0; p < SP_COUNT; p++) if (t.piece[p] >= PIECE_COUNT[p]) return false;
    *s = t; return true;
}

bool skin_parse_hex(const char *text, uint8_t rgb[3]) {
    while (*text == ' ') text++;
    if (*text == '#') text++;
    char d[7]; int n = 0;
    while (*text && *text != ' ' && n < 7) { if (!isxdigit((unsigned char)*text)) return false; d[n++] = *text++; }
    if (n == 3) { char e[7] = { d[0], d[0], d[1], d[1], d[2], d[2], 0 }; memcpy(d, e, 7); n = 6; }   // "F80" -> "FF8800"
    if (n != 6) return false;
    d[6] = 0;
    unsigned v = 0; sscanf(d, "%x", &v);
    rgb[0] = (v >> 16) & 255; rgb[1] = (v >> 8) & 255; rgb[2] = v & 255;
    return true;
}

void skin_hex(const uint8_t rgb[3], char out[8]) { snprintf(out, 8, "#%02X%02X%02X", rgb[0], rgb[1], rgb[2]); }

bool skin_save(const Skin *s, const char *path) {
    uint8_t b[SKIN_BYTES]; skin_pack(s, b);
    FILE *f = fopen(path, "wb"); if (!f) return false;
    fwrite(b, 1, sizeof b, f); fclose(f); return true;
}
bool skin_load(Skin *s, const char *path) {
    uint8_t b[SKIN_BYTES]; FILE *f = fopen(path, "rb"); if (!f) return false;
    size_t n = fread(b, 1, sizeof b, f); fclose(f);
    return n == sizeof b && skin_unpack(s, b);
}
