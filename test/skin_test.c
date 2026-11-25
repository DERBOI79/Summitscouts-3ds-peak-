#include <stdio.h>
#include <string.h>
#include "skin.h"
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
int main(void) {
    int fails = 0; uint8_t c[3]; char h[8];
    CHECK(skin_parse_hex("FF8800", c) && c[0] == 255 && c[1] == 136 && c[2] == 0);
    CHECK(skin_parse_hex("#ff8800", c) && c[0] == 255 && c[1] == 136);
    CHECK(skin_parse_hex("f80", c) && c[0] == 255 && c[1] == 136 && c[2] == 0);
    CHECK(!skin_parse_hex("GG0000", c));
    CHECK(!skin_parse_hex("12345", c));
    CHECK(!skin_parse_hex("1234567", c));
    CHECK(!skin_parse_hex("", c));
    skin_hex(c, h); CHECK(strlen(h) == 7);
    for (int i = 0; i < SKIN_PRESETS; i++) {
        Skin a, b; uint8_t buf[SKIN_BYTES]; skin_preset(&a, i); skin_pack(&a, buf);
        CHECK(skin_unpack(&b, buf)); CHECK(memcmp(&a, &b, sizeof a) == 0);
    }
    Skin s; uint8_t bad[SKIN_BYTES]; skin_pack(&(Skin){0}, bad);
    bad[0] = 9; CHECK(!skin_unpack(&s, bad));                       // wrong version
    skin_default(&s); skin_pack(&s, bad); bad[1 + SC_COUNT * 3] = 77; CHECK(!skin_unpack(&s, bad));   // piece index out of range
    skin_default(&s); CHECK(skin_save(&s, "/tmp/skin_test.bin")); Skin l; CHECK(skin_load(&l, "/tmp/skin_test.bin") && !memcmp(&s, &l, sizeof s));
    printf(fails ? "%d FAILED\n" : "all skin tests passed\n", fails);
    return fails != 0;
}
