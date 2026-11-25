#include <stdio.h>
#include <assert.h>
#include "../source/game.h"
static Game g;
static void setup(int climb, int hooked, uint32_t held) {
    game_init(&g, 777);
    float bx = 0, bz = 0; int f = 0;
    for (float x = -800; x < 3000 && !f; x += 8) for (float z = 0; z < 3000 && !f; z += 8)
        if (world_height(&g.w, x, z) > 100 && world_slope(&g.w, x, z, 0, 0) > 0.9f) { bx = x; bz = z; f = 1; }
    g.x = bx; g.z = bz; g.y = world_height(&g.w, bx, bz); g.state = climb ? PS_CLIMB : PS_GROUND;
    if (!climb) { for (float x = 0; x < 500; x += 4) if (world_slope(&g.w, x, 0, 0, 0) < 0.2f) { g.x = x; g.z = 0; g.y = world_height(&g.w, x, 0); break; } }
    g.hooked = hooked; g.stamina = 50; g.hunger = 100; g.wx = g.wxBase = WX_CLEAR; g.rockTimer = 1e9f; g.ambient = 20;
}
static float run(uint32_t held) {
    Input in = {0}; in.held = held;
    for (int i = 0; i < 180; i++) { g.hunger = 100; g.exposure = 0; game_update(&g, 1.0f / 60, &in); }
    return g.stamina;
}
int main() {
    setup(1, 0, 0); float a = run(BTN_GRIP_L | BTN_GRIP_R); printf("climbing, two hands, 3s: %.1f (start 50)\n", a); assert(a <= 50.01f && g.state == PS_CLIMB);
    setup(1, 1, 0);  float b = run(0); printf("hooked, no hands, 3s: %.1f state=%d\n", b, g.state); assert(b > 60 && g.state == PS_CLIMB);
    setup(1, 0, 0);  run(0); printf("not hooked, no hands: state=%d (2=fall)\n", g.state); assert(g.state == PS_FALL);
    setup(0, 0, 0);  float c = run(0); printf("flat ground, 3s: %.1f\n", c); assert(c > 80);
    printf("stamina tests passed\n");
}
