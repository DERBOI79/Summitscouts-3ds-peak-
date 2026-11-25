#include <stdio.h>
#include <math.h>
#include <assert.h>
#include "../source/game.h"
// Put the player on a steep face, force the timer, check: warning lasts 10 s, standing still gets hit, stepping aside does not.
static int run(int dodge, float *firstWarn, float *hitAt) {
    static Game g; game_init(&g, 777);
    // find a steep spot high enough
    float bx = 0, bz = 0; int found = 0;
    for (float x = -800; x < 3000 && !found; x += 8) for (float z = 0; z < 3000 && !found; z += 8)
        if (world_height(&g.w, x, z) > 100 && world_slope(&g.w, x, z, 0, 0) > 0.9f) { bx = x; bz = z; found = 1; }
    assert(found);
    g.x = bx; g.z = bz; g.y = world_height(&g.w, bx, bz); g.state = PS_CLIMB; g.rockTimer = 0.01f; g.wx = g.wxBase = WX_CLEAR; g.wxEpoch = 0;
    Input in = {0}; in.held = BTN_GRIP_L | BTN_GRIP_R;
    float t = 0; *firstWarn = -1; *hitAt = -1; float sx = 0, sz = 0; int dirset = 0;
    for (int i = 0; i < 60 * 25; i++, t += 1.0f / 60) {
        g.health = 100; g.stamina = 100; g.hunger = 100; g.exposure = 0; g.ambient = 20;
        if (g.rockOn && *firstWarn < 0) { *firstWarn = t; sx = -g.rockUz; sz = g.rockUx; dirset = 1; }
        if (dodge && dirset && g.rockWarn > 0) {   // walk sideways at climbing speed
            g.x += sx * 0.9f / 60; g.z += sz * 0.9f / 60; g.y = world_height(&g.w, g.x, g.z);
        }
        float h0 = g.health;
        game_update(&g, 1.0f / 60, &in);
        if (g.rockHit && *hitAt < 0) *hitAt = t;
        (void)h0;
        if (g.state == PS_FALL) break;
    }
    return g.rockHit;
}
int main() {
    float w, h;
    int hit = run(0, &w, &h);
    printf("stand still: hit=%d warning at %.2fs, hit at %.2fs (gap %.2fs)\n", hit, w, h, h - w);
    assert(hit && h - w > 9.5f);
    int hit2 = run(1, &w, &h);
    printf("sidestep: hit=%d\n", hit2);
    assert(!hit2);
    printf("rock tests passed\n");
}
