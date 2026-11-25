#include <stdio.h>
#include <string.h>
#include "game.h"
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
int main(void) {
    int fails = 0; static Game g; game_init(&g, 12345); Input in; memset(&in, 0, sizeof in);
    game_update(&g, 0.03f, &in); CHECK(g.wx != WX_THUNDER);
    g.y = g.w.sh * 0.92f; g.x = g.w.sx; g.z = g.w.sz; g.state = PS_CLIMB; g.stamina = 100; g.hang = 100;
    game_update(&g, 0.03f, &in); CHECK(g.wx == WX_THUNDER);
    int flashes = 0; for (int i = 0; i < 30 * 60; i++) { float f0 = g.flash; g.y = g.w.sh * 0.92f; game_update(&g, 1 / 30.f, &in); if (g.flash > f0) flashes++; }
    printf("lightning flashes in 60 s: %d\\n", flashes); CHECK(flashes >= 5 && flashes <= 25);
    // slipperiness: same climb drains more in thunder than in clear weather
    static Game a, b; game_init(&a, 12345); game_init(&b, 12345);
    a.state = b.state = PS_CLIMB; a.y = b.y = 100; a.x = b.x = 0; a.z = b.z = 0; a.gripL = 1;
    in.held = BTN_GRIP_L; a.wx = a.wxBase = WX_CLEAR; a.stamina = b.stamina = 100;
    for (int i = 0; i < 90; i++) { a.y = b.y = 100; a.state = b.state = PS_CLIMB; game_update(&a, 1 / 30.f, &in); b.y = b.w.sh * 0.92f; b.x = b.w.sx; b.z = b.w.sz; game_update(&b, 1 / 30.f, &in); }
    printf("stamina after 3 s one-handed: clear %.1f, thunderstorm %.1f\n", a.stamina, b.stamina);
    CHECK(b.stamina < a.stamina);
    game_init(&g, 12345); g.y = g.w.sh * 0.97f; game_update(&g, 0.03f, &in); CHECK(g.wx == WX_SUNSET);
    g.y = g.w.sh * 0.92f; game_update(&g, 0.03f, &in); CHECK(g.wx == WX_THUNDER);
    game_init(&g, 12345); g.y = g.w.sh * 0.5f; game_update(&g, 0.03f, &in); CHECK(g.wx != WX_THUNDER);
    printf(fails ? "%d FAILED\n" : "storm tests passed\n", fails); return fails != 0;
}
