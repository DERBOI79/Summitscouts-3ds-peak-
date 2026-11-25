// Summit Scouts - citro3d renderer: low-poly chunked terrain, trees, fog, first-person hands.
#include <3ds.h>
#include <citro3d.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include "render.h"
#include "vshader_shbin.h"

#define DISPLAY_TRANSFER_FLAGS \
    (GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) | \
     GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) | \
     GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))

typedef struct { float x, y, z, r, g, b; } Vert;

#define CS 16
#define CELL 4.0f
#define CHUNK (CS * CELL)
#define VCAP 4800
#define NSLOT 25
#define DYN_CAP 16384

typedef struct { int cx, cz, valid, n; Vert *v; } Chunk;

static C3D_RenderTarget *top;
static DVLB_s *vsh_dvlb;
static shaderProgram_s prog;
static int uProj, uMV, uFogP, uFogC, uTint;
static C3D_Mtx proj;
static Chunk chunks[NSLOT];
static Vert *dyn; static int dynN;
static float fogCol[3] = { 0.55f, 0.78f, 0.65f };

static const float GROUND[B_COUNT][3] = {
    { 0.20f, 0.55f, 0.22f }, { 0.80f, 0.45f, 0.28f }, { 0.30f, 0.42f, 0.25f },
    { 0.35f, 0.60f, 0.28f }, { 0.93f, 0.96f, 1.00f }, { 0.93f, 0.80f, 0.50f }, { 0.24f, 0.20f, 0.22f } };
static const float ROCK[B_COUNT][3] = {
    { 0.45f, 0.42f, 0.38f }, { 0.72f, 0.38f, 0.26f }, { 0.35f, 0.38f, 0.30f },
    { 0.50f, 0.48f, 0.46f }, { 0.60f, 0.66f, 0.76f }, { 0.80f, 0.62f, 0.42f }, { 0.34f, 0.26f, 0.27f } };
static const float FOG[B_COUNT][3] = {
    { 0.55f, 0.78f, 0.65f }, { 0.90f, 0.62f, 0.48f }, { 0.50f, 0.58f, 0.42f },
    { 0.65f, 0.76f, 0.82f }, { 0.88f, 0.93f, 1.00f }, { 0.96f, 0.86f, 0.62f }, { 0.58f, 0.27f, 0.20f } };
static const float WXDIM[WX_COUNT] = { 1.0f, 0.88f, 0.75f, 0.60f, 0.50f, 1.0f };
static const float SUNSET_FOG[3] = { 0.98f, 0.60f, 0.52f };

static float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static float mixf(float a, float b, float t) { return a + (b - a) * t; }

// ---------- vertex helpers ----------
static void push_tri(Vert *buf, int *n, int cap, const float *a, const float *b, const float *c, const float *col) {
    if (*n + 3 > cap) return;
    const float *p[3] = { a, b, c };
    for (int i = 0; i < 3; i++) { Vert *q = &buf[(*n)++]; q->x = p[i][0]; q->y = p[i][1]; q->z = p[i][2]; q->r = col[0]; q->g = col[1]; q->b = col[2]; }
}

// Cartoon look: slightly boosted saturation, and light is quantised into hard bands with cool shadows.
static void sat(float *c, float s) { float l = 0.30f * c[0] + 0.59f * c[1] + 0.11f * c[2]; for (int i = 0; i < 3; i++) c[i] = clampf(l + (c[i] - l) * s, 0, 1); }
static void shade(float *out, const float *base, float k) {
    for (int i = 0; i < 3; i++) out[i] = clampf(base[i] * k, 0, 1);
    sat(out, 1.18f);
}
static void cel(float *out, const float *col, float d) {   // d = N.L in 0..1
    float k = d > 0.62f ? 1.05f : (d > 0.28f ? 0.82f : 0.60f);
    for (int i = 0; i < 3; i++) out[i] = clampf(col[i] * k, 0, 1);
    if (d <= 0.28f) { out[0] *= 0.86f; out[1] *= 0.92f; out[2] *= 1.15f; out[2] = clampf(out[2], 0, 1); }
    sat(out, 1.2f);
}

static void cone(Vert *buf, int *n, int cap, float x, float y, float z, float r, float h, int sides, const float *col) {
    float apex[3] = { x, y + h, z };
    for (int i = 0; i < sides; i++) {
        float a0 = i * 6.2831853f / sides, a1 = (i + 1) * 6.2831853f / sides;
        float p0[3] = { x + cosf(a0) * r, y, z + sinf(a0) * r }, p1[3] = { x + cosf(a1) * r, y, z + sinf(a1) * r };
        float c[3]; shade(c, col, 0.72f + 0.28f * (i % 3) / 2.0f);
        push_tri(buf, n, cap, p0, p1, apex, c);
    }
}
static void prism(Vert *buf, int *n, int cap, float x, float y, float z, float r0, float r1, float h, int sides, const float *col) {
    for (int i = 0; i < sides; i++) {
        float a0 = i * 6.2831853f / sides, a1 = (i + 1) * 6.2831853f / sides;
        float b0[3] = { x + cosf(a0) * r0, y, z + sinf(a0) * r0 }, b1[3] = { x + cosf(a1) * r0, y, z + sinf(a1) * r0 };
        float t0[3] = { x + cosf(a0) * r1, y + h, z + sinf(a0) * r1 }, t1[3] = { x + cosf(a1) * r1, y + h, z + sinf(a1) * r1 };
        float c[3]; shade(c, col, 0.7f + 0.3f * (i % 2));
        push_tri(buf, n, cap, b0, b1, t0, c);
        push_tri(buf, n, cap, b1, t1, t0, c);
    }
}
static void box(Vert *buf, int *n, int cap, float x, float y, float z, float sx, float sy, float sz, const float *col) {
    float v[8][3]; for (int i = 0; i < 8; i++) { v[i][0] = x + ((i & 1) ? sx : -sx); v[i][1] = y + ((i & 2) ? sy : -sy); v[i][2] = z + ((i & 4) ? sz : -sz); }
    static const int f[6][4] = { {0,1,3,2}, {4,6,7,5}, {0,2,6,4}, {1,5,7,3}, {0,4,5,1}, {2,3,7,6} };
    static const float sh[6] = { 0.8f, 0.8f, 0.7f, 0.7f, 0.55f, 1.0f };
    for (int i = 0; i < 6; i++) {
        float c[3]; shade(c, col, sh[i]);
        push_tri(buf, n, cap, v[f[i][0]], v[f[i][1]], v[f[i][2]], c);
        push_tri(buf, n, cap, v[f[i][0]], v[f[i][2]], v[f[i][3]], c);
    }
}

// ---------- terrain chunks ----------
static void tree(Chunk *c, const World *w, Biome b, float x, float y, float z, float rnd) {
    float t[3], leaf[3];
    switch (b) {
    case B_RAINFOREST: {
        float tr[3] = { 0.40f, 0.28f, 0.18f }; memcpy(t, tr, sizeof t);
        leaf[0] = 0.10f + 0.1f * rnd; leaf[1] = 0.45f + 0.25f * rnd; leaf[2] = 0.18f;
        float h = 3.0f + 2.5f * rnd;
        prism(c->v, &c->n, VCAP, x, y, z, 0.3f, 0.18f, h, 3, t);
        cone(c->v, &c->n, VCAP, x, y + h - 0.5f, z, 2.3f + rnd, 4.0f, 6, leaf); break; }
    case B_FOREST: {
        float tr[3] = { 0.35f, 0.24f, 0.15f }; memcpy(t, tr, sizeof t);
        leaf[0] = 0.12f; leaf[1] = 0.38f + 0.15f * rnd; leaf[2] = 0.22f;
        prism(c->v, &c->n, VCAP, x, y, z, 0.25f, 0.15f, 2.0f, 3, t);
        cone(c->v, &c->n, VCAP, x, y + 1.5f, z, 1.6f, 7.0f, 5, leaf); break; }
    case B_SWAMP: {
        float tr[3] = { 0.30f, 0.25f, 0.18f }; memcpy(t, tr, sizeof t);
        leaf[0] = 0.40f; leaf[1] = 0.50f; leaf[2] = 0.20f;
        prism(c->v, &c->n, VCAP, x, y, z, 0.22f, 0.15f, 2.2f, 3, t);
        cone(c->v, &c->n, VCAP, x, y + 1.8f, z, 2.0f, 2.2f, 5, leaf); break; }
    case B_DESERT: {
        float g[3] = { 0.25f, 0.60f, 0.30f };
        prism(c->v, &c->n, VCAP, x, y, z, 0.45f, 0.35f, 2.5f + 1.5f * rnd, 4, g); break; }
    case B_LAVA: {                                                       // charred spikes
        float ch[3] = { 0.16f, 0.13f, 0.14f };
        prism(c->v, &c->n, VCAP, x, y, z, 0.28f, 0.05f, 2.5f + 2.0f * rnd, 4, ch); break; }
    case B_SNOWY: {
        float tr[3] = { 0.30f, 0.22f, 0.15f }; memcpy(t, tr, sizeof t);
        leaf[0] = 0.75f; leaf[1] = 0.85f; leaf[2] = 0.85f;
        prism(c->v, &c->n, VCAP, x, y, z, 0.22f, 0.14f, 1.5f, 3, t);
        cone(c->v, &c->n, VCAP, x, y + 1.2f, z, 1.4f, 6.0f, 5, leaf); break; }
    default: break;
    }
    (void)w;
}

static void build_chunk(Chunk *c, const World *w, int cx, int cz) {
    c->cx = cx; c->cz = cz; c->n = 0; c->valid = 1;
    float H[CS + 1][CS + 1]; Biome B[CS][CS];
    for (int j = 0; j <= CS; j++) for (int i = 0; i <= CS; i++) H[j][i] = world_height(w, cx * CHUNK + i * CELL, cz * CHUNK + j * CELL);
    for (int j = 0; j < CS; j++) for (int i = 0; i < CS; i++)
        B[j][i] = world_biome(w, cx * CHUNK + (i + 0.5f) * CELL, cz * CHUNK + (j + 0.5f) * CELL, H[j][i]);
    const float sun[3] = { 0.4f, 0.8f, 0.3f }; const float sl = sqrtf(0.16f + 0.64f + 0.09f);
    for (int j = 0; j < CS; j++) for (int i = 0; i < CS; i++) {
        float x0 = cx * CHUNK + i * CELL, z0 = cz * CHUNK + j * CELL;
        float P[4][3] = { { x0, H[j][i], z0 }, { x0 + CELL, H[j][i + 1], z0 }, { x0, H[j + 1][i], z0 + CELL }, { x0 + CELL, H[j + 1][i + 1], z0 + CELL } };
        static const int T[2][2][3] = { { {0,1,2}, {1,3,2} }, { {0,1,3}, {0,3,2} } };
        Biome b = B[j][i];
        float pd = world_path(w, x0 + 0.5f * CELL, z0 + 0.5f * CELL);
        for (int k = 0; k < 2; k++) {
            const int *t = T[(i + j) & 1][k];
            float *a = P[t[0]], *bb = P[t[1]], *d = P[t[2]];
            float ux = bb[0] - a[0], uy = bb[1] - a[1], uz = bb[2] - a[2], vx = d[0] - a[0], vy = d[1] - a[1], vz = d[2] - a[2];
            float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
            float nl = sqrtf(nx * nx + ny * ny + nz * nz) + 1e-6f; nx /= nl; ny /= nl; nz /= nl;
            if (ny < 0) { nx = -nx; ny = -ny; nz = -nz; }
            float cxm = (a[0] + bb[0] + d[0]) / 3, czm = (a[2] + bb[2] + d[2]) / 3, hm = (a[1] + bb[1] + d[1]) / 3;
            float cliff = clampf((0.80f - ny) / 0.25f, 0, 1);
            float col[3];
            for (int q = 0; q < 3; q++) col[q] = mixf(GROUND[b][q], ROCK[b][q], cliff);
            if (cliff > 0) {
                float gq = world_grip(w, cxm, czm);
                float strata = 1.0f - 0.10f * sinf(hm * 1.3f);                 // texture streaks on every climbable face
                float speck = 0.9f + 0.2f * world_rand(w, (int)floorf(cxm), (int)floorf(czm), 300 + (int)(hm * 0.5f));
                for (int q = 0; q < 3; q++) col[q] *= strata * speck;
                if (gq < 0.30f) { col[0] = mixf(col[0], 0.78f, 0.55f * cliff); col[1] = mixf(col[1], 0.84f, 0.55f * cliff); col[2] = mixf(col[2], 0.92f, 0.55f * cliff); } // slick
            } else {
                float jit = 0.94f + 0.12f * world_rand(w, i + cx * CS, j + cz * CS, 310 + k);
                for (int q = 0; q < 3; q++) col[q] *= jit;
            }
            if (pd < 4.0f) {                                     // the trail: packed dirt, but overgrown
                static const float dirt[3] = { 0.58f, 0.44f, 0.30f }, weed[3] = { 0.22f, 0.48f, 0.20f };
                float edge = 1.0f - clampf((pd - 1.6f) / 2.2f, 0, 1);
                float o = world_rand(w, i + cx * CS, j + cz * CS, 320 + k);
                float wgt = edge * (1.0f - 0.6f * cliff) * (o < 0.40f ? 0.2f : 1.0f);      // patches reclaimed by plants
                for (int q = 0; q < 3; q++) {
                    col[q] = mixf(col[q], dirt[q], wgt);
                    if (o > 0.40f && o < 0.65f) col[q] = mixf(col[q], weed[q], 0.35f * edge);
                }
            }
            float lit = fmaxf(0, (nx * sun[0] + ny * sun[1] + nz * sun[2]) / sl);
            float fc[3]; cel(fc, col, lit);
            if (b == B_LAVA && cliff < 0.2f && world_lava_noise(w, cxm, czm)) {          // molten pool: bright, flat colour
                float f = world_rand(w, i + cx * CS, j + cz * CS, 340 + k);
                fc[0] = 1.0f; fc[1] = 0.38f + 0.30f * f; fc[2] = 0.05f + 0.08f * f;
            }
            push_tri(c->v, &c->n, VCAP, a, bb, d, fc);
        }
        // trees
        static const float dens[B_COUNT] = { 0.11f, 0.0f, 0.04f, 0.08f, 0.025f, 0.012f, 0.03f };
        int gi = cx * CS + i, gj = cz * CS + j;
        float pdens = pd < 2.2f ? 0.0f : (pd < 5.0f ? 0.5f : 1.0f);    // trees thin out over the trail
        if (pd >= 2.2f && pd < 6.0f && c->n < VCAP - 30 && world_rand(w, gi, gj, 230) < 0.30f && b != B_SNOWY && b != B_DESERT) {
            float bx = x0 + 0.5f * CELL + (world_rand(w, gi, gj, 231) - 0.5f) * 2.0f, bz = z0 + 0.5f * CELL + (world_rand(w, gi, gj, 232) - 0.5f) * 2.0f;
            float bc[3] = { 0.16f, 0.46f + 0.15f * world_rand(w, gi, gj, 233), 0.18f };
            cone(c->v, &c->n, VCAP, bx, (P[0][1] + P[3][1]) * 0.5f, bz, 1.1f, 1.0f, 5, bc);   // overgrown bush
        }
        if (world_rand(w, gi, gj, 200) < dens[b] * pdens && fabsf(P[3][1] - P[0][1]) < 1.0f && fabsf(P[1][1] - P[2][1]) < 1.0f && c->n < VCAP - 60) {
            float tx = x0 + 0.5f * CELL + (world_rand(w, gi, gj, 201) - 0.5f) * 2.0f;
            float tz = z0 + 0.5f * CELL + (world_rand(w, gi, gj, 202) - 0.5f) * 2.0f;
            float ty = (P[0][1] + P[1][1] + P[2][1] + P[3][1]) * 0.25f;
            tree(c, w, b, tx, ty, tz, world_rand(w, gi, gj, 203));
        }
    }
    GSPGPU_FlushDataCache(c->v, sizeof(Vert) * c->n);
}

// ---------- static sky: clouds, sunset dome, cloud sea. All built once, never animated ----------
#define NUM_CLOUDS 20
#define CLOUD_VERTS 9000
#define SEA_PUFFS 70
#define SEA_VERTS 19000
#define SEA_GRID 36        // cloud blanket: 36x36 cells
#define SEA_CELL 56.0f     // ~2 km across
#define DOME_VERTS 1200
static Vert *cloudVbo; static int cloudN;
static Vert *seaVbo; static int seaN; static uint32_t seaSeed; static int seaBuilt;
static Vert *domeVbo; static int domeN;

static void puff(Vert *buf, int *n, int cap, int pal, float x, float y, float z, float rx, float ry, float rz, int R, int S) {
    for (int i = 0; i < R; i++) for (int j = 0; j < S; j++) {
        float la0 = -1.5708f + 3.14159f * i / R, la1 = -1.5708f + 3.14159f * (i + 1) / R, lo0 = 6.28318f * j / S, lo1 = 6.28318f * (j + 1) / S;
        float P[4][3] = {
            { x + rx * cosf(la0) * cosf(lo0), y + ry * sinf(la0), z + rz * cosf(la0) * sinf(lo0) },
            { x + rx * cosf(la0) * cosf(lo1), y + ry * sinf(la0), z + rz * cosf(la0) * sinf(lo1) },
            { x + rx * cosf(la1) * cosf(lo0), y + ry * sinf(la1), z + rz * cosf(la1) * sinf(lo0) },
            { x + rx * cosf(la1) * cosf(lo1), y + ry * sinf(la1), z + rz * cosf(la1) * sinf(lo1) } };
        static const int T[2][3] = { { 0, 1, 2 }, { 1, 3, 2 } };
        for (int k = 0; k < 2; k++) {
            const float *a = P[T[k][0]], *b = P[T[k][1]], *c = P[T[k][2]];
            float ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2], vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
            float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx, l = sqrtf(nx * nx + ny * ny + nz * nz) + 1e-6f;
            nx /= l; ny /= l; nz /= l;
            float mx = (a[0] + b[0] + c[0]) / 3 - x, my = (a[1] + b[1] + c[1]) / 3 - y, mz = (a[2] + b[2] + c[2]) / 3 - z;
            if (nx * mx + ny * my + nz * mz < 0) { nx = -nx; ny = -ny; nz = -nz; }
            float d = nx * 0.3f + ny * 0.8f + nz * 0.4f;
            float col[3];
            if (pal == 2) {                                     // boulder: grey-brown, cel-banded
                if (d > 0.45f) { col[0] = 0.70f; col[1] = 0.64f; col[2] = 0.58f; } else if (d > -0.1f) { col[0] = 0.50f; col[1] = 0.45f; col[2] = 0.42f; } else { col[0] = 0.32f; col[1] = 0.29f; col[2] = 0.34f; }
            } else if (pal == 0) {                                     // daytime sky clouds: white with a blue belly
                col[0] = col[1] = col[2] = 1.0f;
                if (d < -0.65f) { col[0] = 0.78f; col[1] = 0.84f; col[2] = 0.96f; } else if (d < -0.25f) { col[0] = 0.91f; col[1] = 0.95f; col[2] = 1.0f; }
            } else {                                            // sunset cloud sea: lit pink tops, coral sides, violet bellies
                if (d > 0.45f) { col[0] = 1.0f; col[1] = 0.84f; col[2] = 0.78f; }
                else if (d > -0.1f) { col[0] = 0.98f; col[1] = 0.62f; col[2] = 0.60f; }
                else { col[0] = 0.62f; col[1] = 0.48f; col[2] = 0.78f; }
            }
            push_tri(buf, n, cap, a, b, c, col);
        }
    }
}
#define RND() (r = r * 1664525u + 1013904223u, (r >> 8) / 16777216.0f)
static void build_clouds(void) {
    uint32_t r = 0xC10D5u; cloudN = 0;
    for (int i = 0; i < NUM_CLOUDS; i++) {
        float raz = RND(), rel = RND(), rsc = RND();
        float az = raz * 6.2832f, el = 0.12f + rel * 0.63f, rad = 285.0f - 150.0f * (el - 0.12f) / 0.63f;   // low clouds sit far away and fade into the haze
        float cx = sinf(az) * cosf(el) * rad, cy = sinf(el) * rad, cz = -cosf(az) * cosf(el) * rad, sc = 0.7f + rsc * 0.7f;
        for (int p = 0; p < 4; p++) {          // randoms go into locals one at a time so the layout is identical on every compiler
            float jx = RND(), jy = RND(), jz = RND(), sx = RND(), sy = RND(), sz = RND();
            puff(cloudVbo, &cloudN, CLOUD_VERTS, 0, cx + (p - 1.5f) * 28.0f * sc + (jx - 0.5f) * 10.0f, cy + (jy - 0.5f) * 4.0f, cz + (jz - 0.5f) * 14.0f,
                 (24 + sx * 14) * sc, (9 + sy * 5) * sc, (16 + sz * 8) * sc, 3, 6);
        }
    }
    GSPGPU_FlushDataCache(cloudVbo, sizeof(Vert) * cloudN);
}

// The sea of clouds around the peak. World-space and fixed; its tops sit just under the sunset line.
static void build_sea(const World *w) {
    uint32_t r = 0x5EA5u ^ w->seed; seaN = 0;
    float topY = SUMMIT_SUNSET_FRAC * w->sh - 3.0f;
    // 1) a continuous, gently lumpy blanket so there is cloud (not ground) below you in every direction
    for (int j = 0; j < SEA_GRID; j++) for (int i = 0; i < SEA_GRID; i++) {
        float h4[4], p[4][3];
        for (int k = 0; k < 4; k++) {
            int ii = i + (k & 1), jj = j + (k >> 1);
            float x = w->sx + (ii - SEA_GRID / 2) * SEA_CELL, z = w->sz + (jj - SEA_GRID / 2) * SEA_CELL;
            h4[k] = sinf(x * 0.021f + w->seed * 0.001f) * 3.2f + sinf(z * 0.027f + 1.7f) * 3.0f + sinf((x + z) * 0.047f) * 1.8f;
            p[k][0] = x; p[k][1] = topY - 5.0f + h4[k]; p[k][2] = z;
        }
        float lit[3] = { 1.0f, 0.84f, 0.78f }, mid[3] = { 0.98f, 0.66f, 0.62f };
        float avg = (h4[0] + h4[1] + h4[2] + h4[3]) * 0.25f;
        const float *col = avg > 0.8f ? lit : mid;
        push_tri(seaVbo, &seaN, SEA_VERTS, p[0], p[2], p[1], col);
        push_tri(seaVbo, &seaN, SEA_VERTS, p[1], p[2], p[3], col);
    }
    // 2) billowy puffs on top of the blanket
    for (int i = 0; i < SEA_PUFFS; i++) {
        float ang = RND() * 6.2832f, rad = 30.0f + RND() * 600.0f, jy = RND(), sx = RND(), sy = RND(), sz = RND();
        float rx = 70 + sx * 70, ry = 36 + sy * 14, rz = 60 + sz * 60;
        puff(seaVbo, &seaN, SEA_VERTS, 1, w->sx + cosf(ang) * rad, topY - ry + (jy - 0.5f) * 8.0f, w->sz + sinf(ang) * rad, rx, ry, rz, 3, 8);
    }
    GSPGPU_FlushDataCache(seaVbo, sizeof(Vert) * seaN);
    seaSeed = w->seed; seaBuilt = 1;
}
#undef RND

// Sunset dome: vertex-coloured gradient (orange horizon -> pink -> violet -> indigo) plus a low sun with a glow.
static void build_dome(void) {
    static const float EL[8] = { -0.20f, 0.0f, 0.12f, 0.30f, 0.55f, 0.90f, 1.25f, 1.5708f };
    static const float COL[8][3] = { { 1.0f, 0.55f, 0.30f }, { 1.0f, 0.58f, 0.32f }, { 1.0f, 0.52f, 0.45f }, { 0.97f, 0.46f, 0.58f },
                                     { 0.85f, 0.40f, 0.68f }, { 0.62f, 0.36f, 0.72f }, { 0.40f, 0.28f, 0.62f }, { 0.28f, 0.22f, 0.55f } };
    const int S = 20; domeN = 0; const float R = 300.0f;
    for (int i = 0; i < 7; i++) for (int j = 0; j < S; j++) {
        float a0 = 6.28318f * j / S, a1 = 6.28318f * (j + 1) / S, e0 = EL[i], e1 = EL[i + 1];
        float P[4][3] = { { sinf(a0) * cosf(e0) * R, sinf(e0) * R, -cosf(a0) * cosf(e0) * R }, { sinf(a1) * cosf(e0) * R, sinf(e0) * R, -cosf(a1) * cosf(e0) * R },
                          { sinf(a0) * cosf(e1) * R, sinf(e1) * R, -cosf(a0) * cosf(e1) * R }, { sinf(a1) * cosf(e1) * R, sinf(e1) * R, -cosf(a1) * cosf(e1) * R } };
        const float *c0 = COL[i], *c1 = COL[i + 1];
        const int T[2][3] = { { 0, 1, 2 }, { 1, 3, 2 } }; const int RowOf[4] = { 0, 0, 1, 1 };
        for (int k = 0; k < 2; k++) for (int v = 0; v < 3; v++) {
            if (domeN >= DOME_VERTS) break;
            Vert *q = &domeVbo[domeN++]; const float *p = P[T[k][v]], *c = RowOf[T[k][v]] ? c1 : c0;
            q->x = p[0]; q->y = p[1]; q->z = p[2]; q->r = c[0]; q->g = c[1]; q->b = c[2];
        }
    }
    // sun: glow disc behind, bright disc in front (both face the origin; sun sits dead ahead at the start heading, low on the horizon)
    for (int layer = 0; layer < 2; layer++) {
        float rad = layer ? 290.0f : 295.0f, sz = layer ? 24.0f : 70.0f, el = 0.10f;
        float cx = 0, cy = sinf(el) * rad, cz = -cosf(el) * rad; const float *col = layer ? (const float[3]){ 1.0f, 0.95f, 0.65f } : (const float[3]){ 1.0f, 0.72f, 0.48f };
        for (int j = 0; j < 14 && domeN + 3 <= DOME_VERTS; j++) {
            float a0 = 6.28318f * j / 14, a1 = 6.28318f * (j + 1) / 14;
            float P[3][3] = { { cx, cy, cz }, { cx + cosf(a0) * sz, cy + sinf(a0) * sz, cz }, { cx + cosf(a1) * sz, cy + sinf(a1) * sz, cz } };
            for (int v = 0; v < 3; v++) { Vert *q = &domeVbo[domeN++]; q->x = P[v][0]; q->y = P[v][1]; q->z = P[v][2]; q->r = col[0]; q->g = col[1]; q->b = col[2]; }
        }
    }
    GSPGPU_FlushDataCache(domeVbo, sizeof(Vert) * domeN);
}

// ---------- init ----------
void render_init(void) {
    gfxSet3D(false);
    top = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
    C3D_RenderTargetSetOutput(top, GFX_TOP, GFX_LEFT, DISPLAY_TRANSFER_FLAGS);
    vsh_dvlb = DVLB_ParseFile((u32 *)vshader_shbin, vshader_shbin_size);
    shaderProgramInit(&prog);
    shaderProgramSetVsh(&prog, &vsh_dvlb->DVLE[0]);
    C3D_BindProgram(&prog);
    uProj = shaderInstanceGetUniformLocation(prog.vertexShader, "projection");
    uMV   = shaderInstanceGetUniformLocation(prog.vertexShader, "modelView");
    uFogP = shaderInstanceGetUniformLocation(prog.vertexShader, "fogp");
    uFogC = shaderInstanceGetUniformLocation(prog.vertexShader, "fogc");
    uTint = shaderInstanceGetUniformLocation(prog.vertexShader, "tint");
    C3D_AttrInfo *ai = C3D_GetAttrInfo();
    AttrInfo_Init(ai);
    AttrInfo_AddLoader(ai, 0, GPU_FLOAT, 3);
    AttrInfo_AddLoader(ai, 1, GPU_FLOAT, 3);
    C3D_TexEnv *env = C3D_GetTexEnv(0);
    C3D_TexEnvInit(env);
    C3D_TexEnvSrc(env, C3D_Both, GPU_PRIMARY_COLOR, 0, 0);
    C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
    C3D_CullFace(GPU_CULL_NONE);
    Mtx_PerspTilt(&proj, C3D_AngleFromDegrees(65.0f), C3D_AspectRatioTop, 0.15f, 320.0f, false);
    for (int i = 0; i < NSLOT; i++) { chunks[i].v = linearAlloc(sizeof(Vert) * VCAP); chunks[i].valid = 0; chunks[i].cx = chunks[i].cz = 0x7fffffff; }
    dyn = linearAlloc(sizeof(Vert) * DYN_CAP);
    cloudVbo = linearAlloc(sizeof(Vert) * CLOUD_VERTS);
    seaVbo = linearAlloc(sizeof(Vert) * SEA_VERTS);
    domeVbo = linearAlloc(sizeof(Vert) * DOME_VERTS);
    build_clouds(); build_dome();
}

void render_exit(void) {
    for (int i = 0; i < NSLOT; i++) linearFree(chunks[i].v);
    linearFree(dyn);
    linearFree(cloudVbo); linearFree(seaVbo); linearFree(domeVbo);
    shaderProgramFree(&prog);
    DVLB_Free(vsh_dvlb);
}

static void bind_and_draw(Vert *p, int first, int count) {
    if (count <= 0) return;
    C3D_BufInfo *bi = C3D_GetBufInfo();
    BufInfo_Init(bi);
    BufInfo_Add(bi, p, sizeof(Vert), 2, 0x10);
    C3D_DrawArrays(GPU_TRIANGLES, first, count);
}

static void set_fog(float nearD, float farD, const float *col, const float *tint) {
    C3D_FVUnifSet(GPU_VERTEX_SHADER, uFogP, 1.0f / (farD - nearD), -nearD / (farD - nearD), 0, 0);
    C3D_FVUnifSet(GPU_VERTEX_SHADER, uFogC, col[0], col[1], col[2], 1.0f);
    C3D_FVUnifSet(GPU_VERTEX_SHADER, uTint, tint[0], tint[1], tint[2], 1.0f);
}


// ---------- the castaway: big round head, colourful, low poly, ink outline ----------
static void solid_tri(Vert *buf, int *n, int cap, const float *a, const float *b, const float *c, const float *col, const float *ctr) {
    float ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2], vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
    float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
    float l = sqrtf(nx * nx + ny * ny + nz * nz) + 1e-6f; nx /= l; ny /= l; nz /= l;
    float mx = (a[0] + b[0] + c[0]) / 3 - ctr[0], my = (a[1] + b[1] + c[1]) / 3 - ctr[1], mz = (a[2] + b[2] + c[2]) / 3 - ctr[2];
    if (nx * mx + ny * my + nz * mz < 0) { nx = -nx; ny = -ny; nz = -nz; }
    float out[3]; cel(out, col, 0.38f + 0.62f * fmaxf(0, nx * 0.15f + ny * 0.30f - nz * 0.94f));   // front-and-above light: no dark chin
    push_tri(buf, n, cap, a, b, c, out);
}
static void lbox(Vert *buf, int *n, int cap, float x, float y, float z, float hx, float hy, float hz, const float *col) {
    float v[8][3]; for (int i = 0; i < 8; i++) { v[i][0] = x + ((i & 1) ? hx : -hx); v[i][1] = y + ((i & 2) ? hy : -hy); v[i][2] = z + ((i & 4) ? hz : -hz); }
    static const int f[6][4] = { {0,1,3,2}, {4,6,7,5}, {0,2,6,4}, {1,5,7,3}, {0,4,5,1}, {2,3,7,6} };
    float ctr[3] = { x, y, z };
    for (int i = 0; i < 6; i++) { solid_tri(buf, n, cap, v[f[i][0]], v[f[i][1]], v[f[i][2]], col, ctr); solid_tri(buf, n, cap, v[f[i][0]], v[f[i][2]], v[f[i][3]], col, ctr); }
}
static void sphere(Vert *buf, int *n, int cap, float x, float y, float z, float r, const float *col) {
    const int R = 5, S = 8; float ctr[3] = { x, y, z };
    for (int i = 0; i < R; i++) for (int j = 0; j < S; j++) {
        float la0 = -1.5708f + 3.14159f * i / R, la1 = -1.5708f + 3.14159f * (i + 1) / R, lo0 = 6.28318f * j / S, lo1 = 6.28318f * (j + 1) / S;
        float p00[3] = { x + r * cosf(la0) * cosf(lo0), y + r * sinf(la0), z + r * cosf(la0) * sinf(lo0) };
        float p01[3] = { x + r * cosf(la0) * cosf(lo1), y + r * sinf(la0), z + r * cosf(la0) * sinf(lo1) };
        float p10[3] = { x + r * cosf(la1) * cosf(lo0), y + r * sinf(la1), z + r * cosf(la1) * sinf(lo0) };
        float p11[3] = { x + r * cosf(la1) * cosf(lo1), y + r * sinf(la1), z + r * cosf(la1) * sinf(lo1) };
        solid_tri(buf, n, cap, p00, p01, p10, col, ctr); solid_tri(buf, n, cap, p01, p11, p10, col, ctr);
    }
}
static void limb(Vert *buf, int *n, int cap, const float *p0, const float *p1, float r0, float r1, const float *col) {
    float d[3] = { p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2] }; float l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) + 1e-6f;
    for (int i = 0; i < 3; i++) d[i] /= l;
    float h[3] = { 0, 1, 0 }; if (fabsf(d[1]) > 0.9f) { h[0] = 1; h[1] = 0; }
    float u[3] = { d[1] * h[2] - d[2] * h[1], d[2] * h[0] - d[0] * h[2], d[0] * h[1] - d[1] * h[0] };
    float ul = sqrtf(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]) + 1e-6f; for (int i = 0; i < 3; i++) u[i] /= ul;
    float v[3] = { d[1] * u[2] - d[2] * u[1], d[2] * u[0] - d[0] * u[2], d[0] * u[1] - d[1] * u[0] };
    float ctr[3] = { (p0[0] + p1[0]) / 2, (p0[1] + p1[1]) / 2, (p0[2] + p1[2]) / 2 };
    float B[8][3], T[8][3];
    for (int k = 0; k < 8; k++) {
        float a = k * 0.7854f, ca = cosf(a), sa = sinf(a);
        for (int q = 0; q < 3; q++) { float dir = u[q] * ca + v[q] * sa; B[k][q] = p0[q] + dir * r0; T[k][q] = p1[q] + dir * r1; }
    }
    for (int k = 0; k < 8; k++) { int k1 = (k + 1) & 7; solid_tri(buf, n, cap, B[k], B[k1], T[k], col, ctr); solid_tri(buf, n, cap, B[k1], T[k1], T[k], col, ctr); }
}

typedef struct { int state; bool gl, gr; int coat; float phase, climbAnim; } Pose;

static void sphere_lo(Vert *buf, int *n, int cap, float x, float y, float z, float r, const float *col) {   // cheaper ball for hands and feet
    const int R = 3, S = 6; float ctr[3] = { x, y, z };
    for (int i = 0; i < R; i++) for (int j = 0; j < S; j++) {
        float la0 = -1.5708f + 3.14159f * i / R, la1 = -1.5708f + 3.14159f * (i + 1) / R, lo0 = 6.28318f * j / S, lo1 = 6.28318f * (j + 1) / S;
        float p00[3] = { x + r * cosf(la0) * cosf(lo0), y + r * sinf(la0), z + r * cosf(la0) * sinf(lo0) };
        float p01[3] = { x + r * cosf(la0) * cosf(lo1), y + r * sinf(la0), z + r * cosf(la0) * sinf(lo1) };
        float p10[3] = { x + r * cosf(la1) * cosf(lo0), y + r * sinf(la1), z + r * cosf(la1) * sinf(lo0) };
        float p11[3] = { x + r * cosf(la1) * cosf(lo1), y + r * sinf(la1), z + r * cosf(la1) * sinf(lo1) };
        solid_tri(buf, n, cap, p00, p01, p10, col, ctr); solid_tri(buf, n, cap, p01, p11, p10, col, ctr);
    }
}
static void ell(Vert *buf, int *n, int cap, float x, float y, float z, float rx, float ry, float rz, const float *col, int R, int S) {   // squashed ball: the basis of the round look
    float ctr[3] = { x, y, z };
    for (int i = 0; i < R; i++) for (int j = 0; j < S; j++) {
        float la0 = -1.5708f + 3.14159f * i / R, la1 = -1.5708f + 3.14159f * (i + 1) / R, lo0 = 6.28318f * j / S, lo1 = 6.28318f * (j + 1) / S;
        float p00[3] = { x + rx * cosf(la0) * cosf(lo0), y + ry * sinf(la0), z + rz * cosf(la0) * sinf(lo0) };
        float p01[3] = { x + rx * cosf(la0) * cosf(lo1), y + ry * sinf(la0), z + rz * cosf(la0) * sinf(lo1) };
        float p10[3] = { x + rx * cosf(la1) * cosf(lo0), y + ry * sinf(la1), z + rz * cosf(la1) * sinf(lo0) };
        float p11[3] = { x + rx * cosf(la1) * cosf(lo1), y + ry * sinf(la1), z + rz * cosf(la1) * sinf(lo1) };
        solid_tri(buf, n, cap, p00, p01, p10, col, ctr); solid_tri(buf, n, cap, p01, p11, p10, col, ctr);
    }
}
static void lerp3(float *o, const float *a, const float *b, float t) { for (int i = 0; i < 3; i++) o[i] = a[i] + (b[i] - a[i]) * t; }
static void dim(float *o, const float *c, float k) { for (int i = 0; i < 3; i++) o[i] = clampf(c[i] * k, 0, 1); }

// Builds the castaway in model space (facing -z, feet at y=0) from a Skin: any colours, any outfit pieces.
static void build_character(Vert *buf, int *n, int cap, const Skin *sk, const Pose *po) {
    float C[SC_COUNT][3];
    for (int k = 0; k < SC_COUNT; k++) for (int q = 0; q < 3; q++) C[k][q] = sk->rgb[k][q] / 255.0f;
    static const float white[3] = { 1, 1, 1 }, blush[3] = { 1.0f, 0.55f, 0.62f }, roll[3] = { 0.95f, 0.80f, 0.30f };
    static const float coats[4][3] = { { 0, 0, 0 }, { 0.98f, 0.85f, 0.25f }, { 0.25f, 0.55f, 0.95f }, { 0.95f, 0.95f, 1.0f } };
    const int top = sk->piece[SP_TOP], bot = sk->piece[SP_BOTTOM], shoe = sk->piece[SP_SHOES], hat = sk->piece[SP_HAT], face = sk->piece[SP_FACE];
    bool climbing = po->state == PS_CLIMB, falling = po->state == PS_FALL;
    float sw = sinf(po->phase), bob = climbing ? 0 : fabsf(sw) * 0.05f, ph = sinf(po->climbAnim * 3.14159f);

    // ---- head, hair, face ----
    ell(buf, n, cap, 0, 1.82f + bob, 0, 0.46f, 0.42f, 0.43f, C[SC_SKIN], 6, 12);
    if (hat != 1) ell(buf, n, cap, 0, 2.06f + bob, 0.07f, 0.38f, 0.33f, 0.36f, C[SC_HAIR], 5, 10);
    sphere_lo(buf, n, cap, -0.40f, 1.84f + bob, 0.14f, 0.16f, C[SC_HAIR]);
    sphere_lo(buf, n, cap, 0.40f, 1.84f + bob, 0.14f, 0.16f, C[SC_HAIR]);
    for (int s = -1; s <= 1; s += 2) {
        float ex = s * 0.16f, ey = 1.82f + bob;
        if (face == 0) { lbox(buf, n, cap, ex, ey, -0.425f, 0.055f, 0.08f, 0.02f, C[SC_EYES]); lbox(buf, n, cap, ex + 0.02f, ey + 0.04f, -0.447f, 0.02f, 0.025f, 0.01f, white); }
        else if (face == 1) { lbox(buf, n, cap, ex, ey, -0.425f, 0.07f, 0.095f, 0.02f, C[SC_EYES]);
            lbox(buf, n, cap, ex + 0.025f, ey + 0.05f, -0.447f, 0.025f, 0.03f, 0.01f, white); lbox(buf, n, cap, ex - 0.02f, ey - 0.04f, -0.447f, 0.015f, 0.015f, 0.01f, white); }
        else { lbox(buf, n, cap, ex, ey - 0.01f, -0.425f, 0.07f, 0.02f, 0.02f, C[SC_EYES]); lbox(buf, n, cap, ex, ey + 0.03f, -0.43f, 0.075f, 0.012f, 0.015f, C[SC_HAIR]); }
        lbox(buf, n, cap, s * 0.27f, 1.70f + bob, -0.40f, 0.05f, 0.025f, 0.01f, blush);
    }
    // ---- hats ----
    if (hat == 1) {                                       // beanie
        ell(buf, n, cap, 0, 2.00f + bob, 0.02f, 0.50f, 0.42f, 0.47f, C[SC_HAT], 5, 10);
        float cuff[3]; dim(cuff, C[SC_HAT], 0.8f);
        float a[3] = { 0, 1.95f + bob, 0.02f }, b[3] = { 0, 2.03f + bob, 0.02f }; limb(buf, n, cap, a, b, 0.50f, 0.50f, cuff);
        float pomc[3]; lerp3(pomc, C[SC_HAT], white, 0.5f); sphere_lo(buf, n, cap, 0, 2.44f + bob, 0.02f, 0.10f, pomc);
    } else if (hat == 2) {                                // bucket hat
        float a[3] = { 0, 2.07f + bob, 0 }, b[3] = { 0, 2.12f + bob, 0 }, c[3] = { 0, 2.38f + bob, 0 }, brim[3]; dim(brim, C[SC_HAT], 0.85f);
        limb(buf, n, cap, a, b, 0.70f, 0.56f, brim); limb(buf, n, cap, b, c, 0.46f, 0.40f, C[SC_HAT]);
    } else if (hat == 3) {                                // cap with visor
        ell(buf, n, cap, 0, 2.02f + bob, 0.0f, 0.50f, 0.42f, 0.47f, C[SC_HAT], 5, 10);
        float visor[3]; dim(visor, C[SC_HAT], 0.8f); ell(buf, n, cap, 0, 2.00f + bob, -0.50f, 0.27f, 0.035f, 0.20f, visor, 3, 8);
    }
    // ---- torso: gameplay coat (warmth) overrides the cosmetic top ----
    bool coat = po->coat > 0;
    static const float TW[4] = { 0.25f, 0.31f, 0.29f, 0.32f }, TH[4] = { 0.30f, 0.30f, 0.31f, 0.33f }, TD[4] = { 0.15f, 0.19f, 0.18f, 0.20f };
    static const float SR[4] = { 0.11f, 0.16f, 0.13f, 0.15f }, SF[4] = { 0.45f, 0.65f, 1.0f, 1.0f };
    const float *tc = coat ? coats[po->coat] : C[SC_TOP];
    float tw = coat ? 0.31f : TW[top], th = coat ? 0.33f : TH[top], td = coat ? 0.20f : TD[top], sr = coat ? 0.13f : SR[top], sf = coat ? 1.0f : SF[top];
    ell(buf, n, cap, 0, 1.17f + bob, 0, tw * 1.12f, th * 1.25f, td * 1.45f, tc, 5, 10);   // round belly
    if (!coat && top == 2) {                              // hoodie: hood bunched at the neck + front pouch
        float hc[3]; dim(hc, tc, 0.9f); sphere_lo(buf, n, cap, 0, 1.66f + bob, 0.17f, 0.22f, hc); ell(buf, n, cap, 0, 1.02f + bob, -0.24f, 0.20f, 0.07f, 0.05f, hc, 3, 8);
    } else if (!coat && top == 3) {                       // puffer: collar + quilted band
        float hc[3]; dim(hc, tc, 0.85f); ell(buf, n, cap, 0, 1.58f + bob, 0, 0.27f, 0.08f, 0.22f, hc, 3, 8); ell(buf, n, cap, 0, 1.17f + bob, 0, tw * 1.14f, 0.035f, td * 1.48f, hc, 3, 10);
    }
    if (po->coat == 3) { float fur[3] = { 0.98f, 0.98f, 1 }; sphere_lo(buf, n, cap, 0, 1.64f + bob, 0.15f, 0.24f, fur); }
    // ---- pelvis + pack ----
    ell(buf, n, cap, 0, 0.84f + bob, 0, bot == 1 ? 0.32f : 0.28f, 0.17f, bot == 1 ? 0.23f : 0.20f, C[SC_BOTTOM], 4, 10);
    {   // climbing harness: webbing belt, thigh loops and a gold D-ring at the front where the rope clips in
        static const float web[3] = { 0.95f, 0.50f, 0.12f }, gold[3] = { 1.0f, 0.80f, 0.25f };
        ell(buf, n, cap, 0, 0.99f + bob, 0, 0.345f, 0.03f, 0.265f, web, 2, 10);
        for (int s = -1; s <= 1; s += 2) ell(buf, n, cap, s * 0.15f, 0.70f + bob, 0, 0.15f, 0.025f, 0.15f, web, 2, 8);
        lbox(buf, n, cap, 0, 0.99f + bob, -0.285f, 0.07f, 0.012f, 0.012f, gold); lbox(buf, n, cap, 0, 0.90f + bob, -0.285f, 0.07f, 0.012f, 0.012f, gold);
        lbox(buf, n, cap, -0.07f, 0.945f + bob, -0.285f, 0.012f, 0.057f, 0.012f, gold); lbox(buf, n, cap, 0.07f, 0.945f + bob, -0.285f, 0.012f, 0.057f, 0.012f, gold);
    }
    ell(buf, n, cap, 0, 1.20f + bob, 0.36f, 0.22f, 0.30f, 0.13f, C[SC_PACK], 4, 8);
    ell(buf, n, cap, 0, 1.58f + bob, 0.36f, 0.24f, 0.08f, 0.11f, roll, 3, 8);
    // ---- arms and legs ----
    for (int s = -1; s <= 1; s += 2) {
        float sh[3] = { s * (tw * 1.12f + 0.04f), 1.40f + bob, 0 }, hand[3], hip[3] = { s * 0.14f, 0.80f + bob, 0 }, foot[3];
        bool grip = s < 0 ? po->gl : po->gr; float side = (float)s;
        if (climbing) {
            hand[0] = side * 0.30f; hand[1] = grip ? 2.05f + side * ph * 0.12f : 1.1f; hand[2] = grip ? -0.45f : -0.2f;
            foot[0] = side * 0.2f; foot[1] = 0.45f - side * ph * 0.12f; foot[2] = -0.40f;
        } else if (falling) {
            hand[0] = side * 0.65f; hand[1] = 2.0f + sinf(po->phase * 2 + side) * 0.2f; hand[2] = -0.1f;
            foot[0] = side * 0.32f; foot[1] = 0.25f; foot[2] = 0.12f * side;
        } else {
            hand[0] = side * 0.38f; hand[1] = 0.98f; hand[2] = side * sw * 0.32f;
            foot[0] = side * 0.14f; foot[1] = 0.10f + fmaxf(0, -side * sw) * 0.18f; foot[2] = -side * sw * 0.30f;
        }
        float mid[3]; lerp3(mid, sh, hand, sf);
        sphere_lo(buf, n, cap, sh[0], sh[1], sh[2], sr + 0.03f, tc);          // round shoulder
        limb(buf, n, cap, sh, mid, sr + 0.01f, sr, tc);                       // sleeve
        if (sf < 1.0f) limb(buf, n, cap, mid, hand, 0.095f, 0.085f, C[SC_SKIN]); // bare forearm
        sphere_lo(buf, n, cap, hand[0], hand[1], hand[2], 0.12f, C[SC_SKIN]);
        // trousers
        if (bot == 0) {                                                       // shorts
            float k[3]; lerp3(k, hip, foot, 0.40f); limb(buf, n, cap, hip, k, 0.15f, 0.14f, C[SC_BOTTOM]); limb(buf, n, cap, k, foot, 0.11f, 0.10f, C[SC_SKIN]);
        } else if (bot == 1) {                                                // baggy jeans: wide, pooling over the shoes
            limb(buf, n, cap, hip, foot, 0.19f, 0.23f, C[SC_BOTTOM]);
            float fold[3]; dim(fold, C[SC_BOTTOM], 0.75f); lbox(buf, n, cap, foot[0], foot[1] + 0.12f, foot[2], 0.2f, 0.025f, 0.2f, fold);
        } else if (bot == 2) {                                                // cargo pants with side pockets
            limb(buf, n, cap, hip, foot, 0.16f, 0.15f, C[SC_BOTTOM]);
            float k[3], pk[3]; lerp3(k, hip, foot, 0.40f); dim(pk, C[SC_BOTTOM], 0.72f);
            lbox(buf, n, cap, k[0] + side * 0.14f, k[1], k[2], 0.04f, 0.08f, 0.07f, pk);
        } else {                                                              // joggers: tapered, elastic cuff
            limb(buf, n, cap, hip, foot, 0.15f, 0.10f, C[SC_BOTTOM]);
            float cu[3]; dim(cu, C[SC_BOTTOM], 0.7f); lbox(buf, n, cap, foot[0], foot[1] + 0.09f, foot[2], 0.095f, 0.03f, 0.095f, cu);
        }
        // shoes
        if (shoe == 0) {                                                      // hiking boots
            ell(buf, n, cap, foot[0], foot[1], foot[2] - 0.05f, 0.15f, 0.12f, 0.21f, C[SC_SHOES], 3, 8);
        } else if (shoe == 1) {                                               // chunky sneakers
            ell(buf, n, cap, foot[0], foot[1] + 0.04f, foot[2] - 0.07f, 0.15f, 0.10f, 0.23f, C[SC_SHOES], 3, 8);
            ell(buf, n, cap, foot[0], foot[1] - 0.05f, foot[2] - 0.07f, 0.16f, 0.05f, 0.25f, white, 3, 8);
        } else {                                                              // high-tops
            ell(buf, n, cap, foot[0], foot[1] + 0.09f, foot[2] - 0.05f, 0.14f, 0.16f, 0.20f, C[SC_SHOES], 4, 8);
            ell(buf, n, cap, foot[0], foot[1] - 0.05f, foot[2] - 0.06f, 0.15f, 0.045f, 0.23f, white, 3, 8);
        }
    }
}

static void add_outline(Vert *buf, int *n, int cap, int from, int to) {          // dark, slightly bigger copy, drawn behind
    for (int i = from; i < to && *n < cap; i++) {
        Vert v = buf[i]; v.x *= 1.07f; v.y = (v.y - 1.0f) * 1.07f + 1.0f; v.z *= 1.07f; v.r = 0.06f; v.g = 0.05f; v.b = 0.09f; buf[(*n)++] = v;
    }
}

// ---------- summit flag (waves) and cosmetic wind streaks ----------
#define FLAG_W 8.0f
#define FLAG_H 4.6f
#define FLAG_POLE 15.0f
static void flag_pt(float *out, const float *p, float u, float v, float t) {      // u: 0 at the pole .. 1 at the tip, v: 0 top .. 1 bottom
    float amp = 0.6f * u;
    out[0] = p[0] + u * FLAG_W;
    out[1] = p[1] + FLAG_POLE - 0.4f - v * FLAG_H + sinf(t * 2.1f - u * 4.0f) * 0.18f * u;
    out[2] = p[2] + sinf(t * 3.0f - u * 5.0f + v * 1.5f) * amp;
}
static void build_summit_flag(Vert *buf, int *n, int cap, const float *p, float t) {
    static const float pole[3] = { 0.92f, 0.92f, 0.95f }, red[3] = { 0.95f, 0.25f, 0.35f }, gold[3] = { 1.0f, 0.85f, 0.25f };
    prism(buf, n, cap, p[0], p[1], p[2], 0.28f, 0.18f, FLAG_POLE, 6, pole);
    sphere_lo(buf, n, cap, p[0], p[1] + FLAG_POLE + 0.15f, p[2], 0.38f, gold);
    const int SEG = 8;
    for (int i = 0; i < SEG; i++) {
        float u0 = (float)i / SEG, u1 = (float)(i + 1) / SEG, a[3], b[3], c[3], d[3];
        flag_pt(a, p, u0, 0, t); flag_pt(b, p, u1, 0, t); flag_pt(c, p, u1, 1, t); flag_pt(d, p, u0, 1, t);
        float k = 0.90f + 0.10f * sinf(t * 3.0f - u0 * 5.0f + 1.0f), col[3] = { red[0] * k, red[1] * k, red[2] * k };    // ripples shade the cloth
        push_tri(buf, n, cap, a, b, c, col); push_tri(buf, n, cap, a, c, d, col);
    }
    float e0[3], e1[3], e2[3];
    flag_pt(e0, p, 0.12f, 0.22f, t); flag_pt(e1, p, 0.48f, 0.5f, t); flag_pt(e2, p, 0.12f, 0.78f, t);
    e0[2] += 0.03f; e1[2] += 0.03f; e2[2] += 0.03f;    // sit a hair in front of the cloth
    push_tri(buf, n, cap, e0, e1, e2, gold);
}

// A little visible wind: thin streaks drifting past you. Purely cosmetic, never touches gameplay.
static void build_wind(Vert *buf, int *n, int cap, const Game *g) {
    float ang = world_rand(&g->w, 3, 4, 55) * 6.2832f, wx = cosf(ang), wz = sinf(ang), t = (float)g->playtime;
    bool windy = g->wx == WX_THUNDER || g->wx == WX_SUNSET;
    int N = windy ? 26 : 12; float sp = g->wx == WX_THUNDER ? 16.0f : (windy ? 11.0f : 8.0f);
    static const float white[3] = { 1.0f, 1.0f, 1.0f };
    for (int i = 0; i < N; i++) {
        float hx = world_rand(&g->w, i, 0, 60) * 40.0f, hy = world_rand(&g->w, i, 1, 60) * 8.0f, hz = world_rand(&g->w, i, 2, 60) * 40.0f;
        float x = hx + wx * t * sp, z = hz + wz * t * sp;                      // world-anchored, repeats every 40 m around you
        x = g->x + fmodf(fmodf(x - g->x, 40.0f) + 60.0f, 40.0f) - 20.0f;
        z = g->z + fmodf(fmodf(z - g->z, 40.0f) + 60.0f, 40.0f) - 20.0f;
        float y = g->y + 0.6f + hy;
        float a[3] = { x, y, z }, b[3] = { x + wx * 3.2f, y + 0.02f, z + wz * 3.2f }, c[3] = { x + wx * 1.4f, y + 0.09f, z + wz * 1.4f };
        push_tri(buf, n, cap, a, b, c, white);
    }
}


// Rockfall: the boulder itself plus, while the warning is running, a red lane on the slope showing where it will come down.
static void build_rock(Vert *buf, int *n, int cap, const Game *g) {
    if (!g->rockOn) return;
    float bx, by, bz;
    game_rock_pos(g, &bx, &by, &bz);
    if (g->rockWarn > -5.0f) puff(buf, n, cap, 2, bx, by, bz, ROCK_RADIUS * 1.1f, ROCK_RADIUS * 0.9f, ROCK_RADIUS, 3, 8);
    if (g->rockWarn > 0) {
        float blink = fmodf((float)g->playtime, 0.5f) < 0.25f ? 1.0f : 0.55f;
        float col[3] = { 1.0f * blink, 0.15f * blink, 0.12f * blink };
        float px = -g->rockUz, pz = g->rockUx;                             // across-slope direction
        for (int i = 0; i < 24; i++) {
            float s0 = i * 6.0f - 12.0f, s1 = s0 + 4.0f, hw = 0.7f;
            float a[4][3];
            for (int k = 0; k < 4; k++) {
                float s = (k & 2) ? s1 : s0, side = (k & 1) ? hw : -hw;
                a[k][0] = g->rockX + g->rockUx * s + px * side; a[k][2] = g->rockZ + g->rockUz * s + pz * side;
                a[k][1] = world_height(&g->w, a[k][0], a[k][2]) + 0.4f;
            }
            push_tri(buf, n, cap, a[0], a[1], a[2], col); push_tri(buf, n, cap, a[1], a[3], a[2], col);
        }
    }
}


// Climbing gear: placed ice screws / hooks with a gold D-ring, and the rope from your anchor to the D-ring on your harness.
static void gear_anchor(Vert *buf, int *n, int cap, const World *w, float x, float y, float z) {
    static const float steel[3] = { 0.78f, 0.80f, 0.86f }, gold[3] = { 1.0f, 0.80f, 0.25f };
    float gx, gz; float sl = world_slope(w, x, z, &gx, &gz);
    float l = hypotf(gx, gz) + 1e-4f, d = 1.1f / (sl > 0.3f ? sl : 0.3f); if (d > 3.0f) d = 3.0f;
    float px = x + gx / l * d, pz = z + gz / l * d, py = world_height(w, px, pz);        // a point on the wall at chest height
    world_slope(w, px, pz, &gx, &gz);
    float nx = -gx, ny = 1.0f, nz = -gz, nl = sqrtf(nx * nx + ny * ny + nz * nz); nx /= nl; ny /= nl; nz /= nl;
    float a[3] = { px, py, pz }, b[3] = { px + nx * 0.30f, py + ny * 0.30f, pz + nz * 0.30f };
    limb(buf, n, cap, a, b, 0.05f, 0.05f, steel);                                       // screw / hook shaft
    float tl = hypotf(nz, nx) + 1e-4f, tx = -nz / tl, tz = nx / tl;                     // along the wall, horizontal
    float c[3] = { px + nx * 0.46f, py + ny * 0.46f - 0.06f, pz + nz * 0.46f };
    float prev[3] = { c[0] + tx * 0.26f, c[1], c[2] + tz * 0.26f };
    for (int i = 1; i <= 6; i++) {                                                      // D-ring
        float an = i * 1.0472f, cs = cosf(an) * 0.26f, sn = sinf(an) * 0.26f;
        float p[3] = { c[0] + tx * cs, c[1] + sn, c[2] + tz * cs };
        limb(buf, n, cap, prev, p, 0.04f, 0.04f, gold);
        prev[0] = p[0]; prev[1] = p[1]; prev[2] = p[2];
    }
}
static void build_gear(Vert *buf, int *n, int cap, const Game *g) {
    static const float rope1[3] = { 0.92f, 0.25f, 0.20f }, rope2[3] = { 0.96f, 0.93f, 0.82f };
    const World *w = &g->w;
    for (int i = 0; i < MAX_ANCHORS; i++)
        if (g->anchors[i].valid && hypotf(g->anchors[i].x - g->x, g->anchors[i].z - g->z) < 120.0f) gear_anchor(buf, n, cap, w, g->anchors[i].x, g->anchors[i].y, g->anchors[i].z);
    if (!g->hooked || g->state != PS_CLIMB) return;
    bool dup = false;
    for (int i = 0; i < MAX_ANCHORS; i++) if (g->anchors[i].valid && hypotf(g->anchors[i].x - g->hookP[0], g->anchors[i].z - g->hookP[2]) < 0.3f) dup = true;
    if (!dup) gear_anchor(buf, n, cap, w, g->hookP[0], g->hookP[1], g->hookP[2]);
    // rope: from the ring to the D-ring on the harness
    float gx, gz, sl = world_slope(w, g->hookP[0], g->hookP[2], &gx, &gz), l = hypotf(gx, gz) + 1e-4f, d = 1.1f / (sl > 0.3f ? sl : 0.3f); if (d > 3.0f) d = 3.0f;
    float ax = g->hookP[0] + gx / l * d, az = g->hookP[2] + gz / l * d, ay = world_height(w, ax, az) + 0.40f;
    float bx = g->x - 0.28f * sinf(g->yaw), by = g->y + 0.95f, bz = g->z - 0.28f * cosf(g->yaw);
    float len = sqrtf((bx - ax) * (bx - ax) + (by - ay) * (by - ay) + (bz - az) * (bz - az));
    float sag = (g->gripL || g->gripR) ? 0.10f * len : 0.015f * len;                    // slack while you climb, taut when you hang
    float prev[3] = { ax, ay, az };
    for (int i = 1; i <= 8; i++) {
        float t = i / 8.0f, sg = 4.0f * t * (1.0f - t) * sag;
        float p[3] = { ax + (bx - ax) * t, ay + (by - ay) * t - sg, az + (bz - az) * t };
        limb(buf, n, cap, prev, p, 0.045f, 0.045f, (i & 1) ? rope1 : rope2);
        prev[0] = p[0]; prev[1] = p[1]; prev[2] = p[2];
    }
}

// ---------- per-frame ----------
// Title / customiser backdrop: your castaway on a little platform, turning slowly.
void render_customizer(const Skin *sk, float angle) {
    static const float bg[3] = { 0.62f, 0.78f, 0.92f }, one[3] = { 1, 1, 1 }, plat[3] = { 0.32f, 0.38f, 0.52f };
    u32 clr = ((u32)(bg[0] * 255) << 24) | ((u32)(bg[1] * 255) << 16) | ((u32)(bg[2] * 255) << 8) | 0xFF;
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C3D_RenderTargetClear(top, C3D_CLEAR_ALL, clr, 0);
    C3D_FrameDrawOn(top);
    C3D_Mtx mv; Mtx_Identity(&mv);
    Mtx_Translate(&mv, 0, -1.15f, -3.4f, true);
    Mtx_RotateY(&mv, angle, true);
    C3D_Mtx mh = mv; Mtx_Translate(&mh, 0, 0, -0.15f, false);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uProj, &proj);
    set_fog(1000.0f, 2000.0f, bg, one);
    C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_ALL);
    dynN = 0;
    float a[3] = { 0, -0.12f, 0 }, b[3] = { 0, 0.0f, 0 }; limb(dyn, &dynN, DYN_CAP, a, b, 1.5f, 1.4f, plat);
    int platEnd = dynN;
    Pose po = { PS_GROUND, false, false, 0, 0.0f, 0.0f };
    build_character(dyn, &dynN, DYN_CAP, sk, &po);
    int bodyEnd = dynN; add_outline(dyn, &dynN, DYN_CAP, platEnd, bodyEnd);
    GSPGPU_FlushDataCache(dyn, sizeof(Vert) * dynN);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uMV, &mh); bind_and_draw(dyn, bodyEnd, dynN - bodyEnd);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uMV, &mv); bind_and_draw(dyn, 0, bodyEnd);
    C3D_FrameEnd(0);
}

static int thirdPerson = 1;
void render_toggle_view(void) { thirdPerson = !thirdPerson; }
int render_is_third_person(void) { return thirdPerson; }

void render_frame(const Game *g, float dt) {
    static const float WHITE_FOG[3] = { 0.55f, 0.78f, 0.65f };
    float tint[3] = { 1, 1, 1 }, farD = 95.0f;
    if (g) {
        const float *tf = g->wx == WX_SUNSET ? SUNSET_FOG : FOG[g->biome];
        float k = clampf(dt * 1.5f, 0, 1);
        float dl = game_daylight(g), br = (0.22f + 0.78f * dl) * WXDIM[g->wx];
        float night[3] = { 0.80f, 0.90f, 1.25f };               // moonlit blue shift
        for (int i = 0; i < 3; i++) {
            float nt = mixf(night[i], 1.0f, dl);
            tint[i] = br * nt;
            fogCol[i] = mixf(fogCol[i], clampf(tf[i] * br * nt, 0, 1), k);
        }
        if (g->wx == WX_SUNSET) { tint[0] = 1.08f; tint[1] = 0.88f; tint[2] = 0.80f; }     // warm light, fixed: no night at the top
        if (g->flash > 0) {                                      // lightning: the whole scene flashes white for a moment
            float fl = clampf(g->flash / 0.18f, 0, 1);
            for (int i = 0; i < 3; i++) { tint[i] += fl * 1.3f; fogCol[i] = mixf(fogCol[i], 1.0f, fl * 0.8f); }
        }
        farD = game_fog_far(g);
    } else memcpy(fogCol, WHITE_FOG, sizeof fogCol);

    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    u32 clr = ((u32)(fogCol[0] * 255) << 24) | ((u32)(fogCol[1] * 255) << 16) | ((u32)(fogCol[2] * 255) << 8) | 0xFF;
    C3D_RenderTargetClear(top, C3D_CLEAR_ALL, clr, 0);
    C3D_FrameDrawOn(top);
    if (!g) { C3D_FrameEnd(0); return; }

    // camera
    float eye = g->state == PS_CLIMB ? 1.35f : 1.6f;
    float ex = g->x, ey = g->y + eye, ez = g->z;
    if (thirdPerson) {                      // orbit camera behind the shoulder, pulled in by terrain
        float cp = cosf(g->pitch), dirx = -sinf(g->yaw) * cp, diry = sinf(g->pitch), dirz = -cosf(g->yaw) * cp;
        float rx = cosf(g->yaw), rz = -sinf(g->yaw), dist = 4.6f, gh;
        for (;;) {
            ex = g->x - dirx * dist + rx * 0.5f; ey = g->y + 1.7f - diry * dist + 0.3f; ez = g->z - dirz * dist + rz * 0.5f;
            gh = world_height(&g->w, ex, ez);
            if (dist <= 1.0f || ey > gh + 0.5f) break;
            dist -= 0.5f;
        }
        if (ey < gh + 0.5f) ey = gh + 0.5f;
    }
    C3D_Mtx mv; Mtx_Identity(&mv);
    Mtx_RotateX(&mv, -g->pitch, true);
    Mtx_RotateY(&mv, -g->yaw, true);
    Mtx_Translate(&mv, -ex, -ey, -ez, true);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uProj, &proj);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uMV, &mv);
    set_fog(farD * 0.06f, farD, fogCol, tint);
    C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_ALL);

    // sky: drawn with a rotation-only matrix, so it stays fixed on the dome (no parallax, no animation)
    {
        C3D_Mtx sk; Mtx_Identity(&sk); Mtx_RotateX(&sk, -g->pitch, true); Mtx_RotateY(&sk, -g->yaw, true);
        C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uMV, &sk);
        if (g->wx == WX_SUNSET) {                                       // cinematic gradient sky + low sun, unfogged
            static const float one[3] = { 1, 1, 1 };
            set_fog(1.0e4f, 2.0e4f, fogCol, one);
            bind_and_draw(domeVbo, 0, domeN);
        }
        float k = g->wx >= WX_RAIN && g->wx != WX_SUNSET ? 0.72f : 1.0f, ct[3] = { tint[0] * k, tint[1] * k, tint[2] * k };
        if (g->wx == WX_SUNSET) { ct[0] = 1.0f; ct[1] = 0.80f; ct[2] = 0.78f; }       // pink-lit clouds
        set_fog(140.0f, 400.0f, fogCol, ct);
        bind_and_draw(cloudVbo, 0, cloudN);
        C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uMV, &mv);
        set_fog(farD * 0.06f, farD, fogCol, tint);
    }
    if (g->y > 0.85f * g->w.sh) {                                       // the sea of clouds lives around the summit
        if (!seaBuilt || seaSeed != g->w.seed) build_sea(&g->w);
        set_fog(120.0f, 700.0f, fogCol, tint);                           // the sea is lit and visible far out, unlike the thick haze on the ground
        bind_and_draw(seaVbo, 0, seaN);
        set_fog(farD * 0.06f, farD, fogCol, tint);
    }

    // stream in terrain, one chunk per frame, nearest first
    int ccx = (int)floorf(g->x / CHUNK), ccz = (int)floorf(g->z / CHUNK), built = 0;
    float fx = -sinf(g->yaw), fz = -cosf(g->yaw);
    for (int ring = 0; ring <= 2; ring++)
        for (int dz = -2; dz <= 2; dz++) for (int dx = -2; dx <= 2; dx++) {
            int r = abs(dx) > abs(dz) ? abs(dx) : abs(dz);
            if (r != ring) continue;
            int cx = ccx + dx, cz = ccz + dz;
            Chunk *c = &chunks[(((cx % 5) + 5) % 5) + 5 * (((cz % 5) + 5) % 5)];
            if (!c->valid || c->cx != cx || c->cz != cz) {
                if (built >= 1 && ring > 0) continue;
                build_chunk(c, &g->w, cx, cz); built++;
            }
            float mx = cx * CHUNK + CHUNK * 0.5f - g->x, mz = cz * CHUNK + CHUNK * 0.5f - g->z;
            if (mx * fx + mz * fz < -50.0f) continue;     // behind the camera
            bind_and_draw(c->v, 0, c->n);
        }

    // dynamic props: loot, camps, checkpoint flags
    dynN = 0;
    const float gold[3] = { 1.0f, 0.85f, 0.2f }, red[3] = { 0.9f, 0.25f, 0.2f }, tent[3] = { 0.95f, 0.55f, 0.15f };
    const float wood[3] = { 0.4f, 0.28f, 0.18f }, flame[3] = { 1.0f, 0.5f, 0.1f };
    for (int cz = (int)floorf(g->z / LOOT_CELL) - 3; cz <= (int)floorf(g->z / LOOT_CELL) + 3; cz++)
        for (int cx = (int)floorf(g->x / LOOT_CELL) - 3; cx <= (int)floorf(g->x / LOOT_CELL) + 3; cx++) {
            float px, pz; uint32_t id;
            if (!world_loot(&g->w, cx, cz, &px, &pz, &id) || game_looted(g, id)) continue;
            float py = world_height(&g->w, px, pz);
            box(dyn, &dynN, DYN_CAP, px, py + 0.4f, pz, 0.35f, 0.3f, 0.35f, gold);
            cone(dyn, &dynN, DYN_CAP, px, py + 0.7f, pz, 0.25f, 1.2f, 4, gold);
        }
    for (int cz = (int)floorf(g->z / CAMP_CELL) - 1; cz <= (int)floorf(g->z / CAMP_CELL) + 1; cz++)
        for (int cx = (int)floorf(g->x / CAMP_CELL) - 1; cx <= (int)floorf(g->x / CAMP_CELL) + 1; cx++) {
            float px, pz;
            if (!world_camp(&g->w, cx, cz, &px, &pz) || hypotf(px - g->x, pz - g->z) > 140.0f) continue;
            float py = world_height(&g->w, px, pz);
            cone(dyn, &dynN, DYN_CAP, px, py, pz, 2.2f, 2.2f, 4, tent);
            prism(dyn, &dynN, DYN_CAP, px + 3.0f, py, pz, 0.5f, 0.35f, 0.4f, 5, wood);
            cone(dyn, &dynN, DYN_CAP, px + 3.0f, py + 0.4f, pz, 0.3f, 0.9f, 4, flame);
            if (!game_looted(g, camp_crate_id(cx, cz))) box(dyn, &dynN, DYN_CAP, px - 2.5f, py + 0.4f, pz + 1.5f, 0.45f, 0.4f, 0.45f, gold);
        }
    for (int k = g->ckpt; k <= g->ckpt + 1 && k <= NUM_CHECKPOINTS; k++) {
        const float *p = g->w.ck[k];
        if (hypotf(p[0] - g->x, p[2] - g->z) > 300.0f) continue;
        if (k == NUM_CHECKPOINTS) { build_summit_flag(dyn, &dynN, DYN_CAP, p, (float)g->playtime); continue; }   // the summit flag
        prism(dyn, &dynN, DYN_CAP, p[0], p[1], p[2], 0.15f, 0.1f, 8.0f, 3, wood);
        float a[3] = { p[0], p[1] + 8.0f, p[2] }, b[3] = { p[0] + 2.2f, p[1] + 7.2f, p[2] }, c[3] = { p[0], p[1] + 6.4f, p[2] };
        push_tri(dyn, &dynN, DYN_CAP, a, b, c, k == g->ckpt ? gold : red);
        push_tri(dyn, &dynN, DYN_CAP, a, c, b, k == g->ckpt ? gold : red);
    }
    build_wind(dyn, &dynN, DYN_CAP, g);
    build_rock(dyn, &dynN, DYN_CAP, g);
    build_gear(dyn, &dynN, DYN_CAP, g);
    int worldN = dynN;

    int bodyEnd = worldN, hullEnd = worldN;
    static float phase = 0, lastx = 0, lastz = 0;
    if (thirdPerson) {
        float sp = hypotf(g->x - lastx, g->z - lastz) / (dt > 0.001f ? dt : 0.016f); if (sp > 8) sp = 0;
        phase += sp * dt * 1.4f; if (g->state != PS_GROUND) phase += dt * 3.0f;
        Pose po = { g->state, g->gripL, g->gripR, g->clothing, phase, g->climbAnim };
        build_character(dyn, &dynN, DYN_CAP, &g->skin, &po);
        bodyEnd = dynN;
        add_outline(dyn, &dynN, DYN_CAP, worldN, bodyEnd);
        hullEnd = dynN;
    } else {
        // first-person hands, built in view space and drawn on top
        static const float coats[4][3] = { { 0, 0, 0 }, { 0.98f, 0.85f, 0.25f }, { 0.25f, 0.55f, 0.95f }, { 0.95f, 0.95f, 1.0f } };
        float skin[3], sleeve[3];
        for (int q = 0; q < 3; q++) { skin[q] = g->skin.rgb[SC_SKIN][q] / 255.0f; sleeve[q] = g->clothing > 0 ? coats[g->clothing][q] : g->skin.rgb[SC_TOP][q] / 255.0f; }
        for (int h = 0; h < 2; h++) {
            bool gripping = h ? g->gripR : g->gripL;
            float s = h ? 1.0f : -1.0f;
            float ph = sinf(g->climbAnim * 3.14159f + (h ? 3.14159f : 0));
            float hx = s * (gripping ? 0.32f : 0.42f);
            float hy = gripping ? 0.18f + 0.12f * ph * (g->state == PS_CLIMB ? 1 : 0) : -0.42f;
            float hz = gripping ? -0.85f : -0.75f;
            box(dyn, &dynN, DYN_CAP, hx, hy, hz, 0.07f, 0.07f, 0.10f, skin);
            box(dyn, &dynN, DYN_CAP, hx + s * 0.03f, hy - 0.12f, hz + 0.28f, 0.09f, 0.08f, 0.22f, sleeve);
        }
    }
    lastx = g->x; lastz = g->z;
    GSPGPU_FlushDataCache(dyn, sizeof(Vert) * dynN);
    bind_and_draw(dyn, 0, worldN);

    if (thirdPerson) {
        C3D_Mtx mc = mv; Mtx_Translate(&mc, g->x, g->y, g->z, true); Mtx_RotateY(&mc, g->yaw, true);
        C3D_Mtx mh = mc; Mtx_Translate(&mh, 0, 0, -0.15f, false);        // nudge the outline away from the camera
        C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uMV, &mh);
        bind_and_draw(dyn, bodyEnd, hullEnd - bodyEnd);
        C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uMV, &mc);
        bind_and_draw(dyn, worldN, bodyEnd - worldN);
    } else {
        C3D_Mtx id; Mtx_Identity(&id);
        C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uMV, &id);
        { static const float one[3] = { 1, 1, 1 }; set_fog(1000.0f, 2000.0f, fogCol, one); }
        C3D_DepthTest(false, GPU_GREATER, GPU_WRITE_ALL);
        bind_and_draw(dyn, worldN, dynN - worldN);
        C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_ALL);
    }

    C3D_FrameEnd(0);
}
