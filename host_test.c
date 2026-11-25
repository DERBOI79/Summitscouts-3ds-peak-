// Host-side simulation: a simple bot climbs the generated mountain using the real game logic.
// Build: make -C .. test      (or: gcc -O2 -I../source host_test.c ../source/game.c ../source/world.c -lm)
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "game.h"

int main(int argc, char **argv) {
    uint32_t seed = argc > 1 ? (uint32_t)strtoul(argv[1], 0, 10) : 12345;
    double maxHours = argc > 2 ? atof(argv[2]) : 6.0;
    static Game g;
    game_init(&g, seed);
    printf("seed %u: summit %.0f m at (%.0f,%.0f)\n", seed, g.w.sh, g.w.sx, g.w.sz);

    // biome census around the start (should be mostly rainforest)
    int cnt[B_COUNT] = {0}, tot = 0;
    for (int zz = -2000; zz <= 2000; zz += 50) for (int xx = -2000; xx <= 2000; xx += 50) {
        float h = world_height(&g.w, xx, zz); cnt[world_biome(&g.w, xx, zz, h)]++; tot++;
    }
    printf("biomes:"); for (int b = 0; b < B_COUNT; b++) printf(" %s %.0f%%", BIOME_NAME[b], 100.0 * cnt[b] / tot); printf("\n");
    int slick = 0, n = 0;
    for (int zz = 0; zz < 400; zz += 2) for (int xx = 0; xx < 400; xx += 2) { slick += world_grip(&g.w, xx, zz) < 0.30f; n++; }
    printf("slick surface fraction: %.1f%%\n", 100.0 * slick / n);

    const float dt = 1.0f / 30.0f;
    int lastCk = 0, falls = 0, deaths = 0; PState prev = g.state; double lastCkT = 0;
    float px = g.x, pz = g.z; int stuck = 0; double handT = 0; int hand = 0;
    for (long i = 0; g.playtime < maxHours * 3600 && !g.won; i++) {
        Input in = {0};
        const float *t = g.w.ck[g.ckpt + 1];
        float dx = t[0] - g.x, dz = t[2] - g.z;
        float want = atan2f(-dx, -dz), dy = want - g.yaw;
        while (dy > 3.14159f) dy -= 6.28318f;
        while (dy < -3.14159f) dy += 6.28318f;
        if (g.state != PS_CLIMB) in.lookx = fmaxf(-0.1f, fminf(0.1f, dy));
        // gear by altitude (what a player would have crafted by then)
        float alt = g.y / g.w.sh;
        int need = alt > 0.62f ? 3 : alt > 0.45f ? 2 : alt > 0.30f ? 1 : 0;
        if (g.clothing < need) g.clothing = need;
        if (g.hunger < 40 && g.inv[ITEM_FOOD] == 0) g.inv[ITEM_FOOD] = 1;
        if (g.hunger < 40) game_use(&g, ITEM_FOOD);

        in.ly = 1;
        if (g.state == PS_GROUND) {
            if (hypot(g.x - px, g.z - pz) < 0.5 * dt * 4) stuck++; else stuck = 0;
            if (stuck > 8) in.held = BTN_GRIP_L | BTN_GRIP_R;          // wall ahead: reach for it
            if (g.stamina < 92 && stuck > 3) { in.ly = 0; in.held = 0; }   // recover on the ground before a climb
        } else if (g.state == PS_CLIMB) {
            handT += dt;
            if (handT > 4.0) { handT = 0; hand ^= 1; }
            // overlap both hands briefly while swapping
            in.held = handT < 0.4 ? (BTN_GRIP_L | BTN_GRIP_R) : (hand ? BTN_GRIP_R : BTN_GRIP_L);
            static int resting; if (g.stamina < 20) resting = 1; if (g.stamina > 55 || g.pump[0] > 80) resting = 0;
            if (resting) in.held = BTN_GRIP_L | BTN_GRIP_R, in.ly = 0;  // hang on two hands and recover
        }
        px = g.x; pz = g.z;
        if (getenv("TRACE") && i % 30 == 0 && g.playtime < atof(getenv("TRACE"))) printf("t=%5.0f st=%d pos(%.0f,%.1f,%.0f) slope=%.2f sta=%.0f hp=%.0f gr=%d%d pump=%.0f,%.0f stuck=%d\n", g.playtime, g.state, g.x, g.y, g.z, g.slopeHere, g.stamina, g.health, g.gripL, g.gripR, g.pump[0], g.pump[1], stuck);
        game_update(&g, dt, &in);
        if (g.state == PS_FALL && prev != PS_FALL) falls++;
        if (getenv("TR2") && g.state != prev && g.playtime > atof(getenv("TR2")) && g.playtime < atof(getenv("TR2")) + 120) printf("t=%6.1f %d->%d pos(%.1f,%.1f,%.1f) slope=%.2f sta=%.0f vy=%.1f msg=%s\n", g.playtime, prev, g.state, g.x, g.y, g.z, g.slopeHere, g.stamina, g.vy, g.msg);
        prev = g.state;
        if (g.ckpt != lastCk) {
            printf("  ckpt %2d at %6.1f min (+%4.1f)  alt %4.0f m  biome %s  wx %s\n", g.ckpt, g.playtime / 60,
                   (g.playtime - lastCkT) / 60, g.y, BIOME_NAME[g.biome], WEATHER_NAME[g.wx]);
            lastCk = g.ckpt; lastCkT = g.playtime;
        }
        if (g.health >= 70 && g.health <= 70.01f && g.exposure == 0 && g.stamina == 100 && g.msgT > 2.9f && g.msg[0] == 'Y') deaths++;
    }
    printf("%s after %.1f min, falls=%d, deaths~%d, ckpt=%d\n", g.won ? "WON" : "not finished", g.playtime / 60, falls, deaths, g.ckpt);
    return 0;
}
