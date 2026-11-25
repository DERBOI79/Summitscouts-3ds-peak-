// Summit Scouts - character customisation data (pure C, host-testable).
// A Skin is small on purpose (30 bytes packed) so it can be sent to friends in co-op.
#pragma once
#include <stdint.h>
#include <stdbool.h>

enum { SC_SKIN, SC_HAIR, SC_EYES, SC_TOP, SC_BOTTOM, SC_SHOES, SC_HAT, SC_PACK, SC_COUNT };
enum { SP_TOP, SP_BOTTOM, SP_SHOES, SP_HAT, SP_FACE, SP_COUNT };

#define SKIN_BYTES 30
#define SKIN_PRESETS 4

typedef struct {
    uint8_t rgb[SC_COUNT][3];     // every colour is a free 24-bit RGB value
    uint8_t piece[SP_COUNT];      // which clothing piece is chosen per category
} Skin;

extern const char *SLOT_NAME[SC_COUNT];
extern const char *PIECE_CAT[SP_COUNT];
extern const int   PIECE_COUNT[SP_COUNT];
extern const char *PIECE_NAME[SP_COUNT][4];

void skin_default(Skin *s);
void skin_preset(Skin *s, int i);                    // ready-made looks for friends
void skin_pack(const Skin *s, uint8_t out[SKIN_BYTES]);
bool skin_unpack(Skin *s, const uint8_t in[SKIN_BYTES]);   // validates, so a bad packet can't crash us
bool skin_parse_hex(const char *text, uint8_t rgb[3]);     // accepts "FF8800" or "#ff8800"
void skin_hex(const uint8_t rgb[3], char out[8]);          // -> "#FF8800"
bool skin_save(const Skin *s, const char *path);
bool skin_load(Skin *s, const char *path);
