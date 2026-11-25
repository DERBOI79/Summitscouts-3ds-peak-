#include "world.h"
#include <math.h>

static uint32_t hash3(int32_t a, int32_t b, uint32_t c) {
    uint32_t h = c * 0x9E3779B1u;
    h ^= (uint32_t)a * 0x85EBCA6Bu; h = (h << 13) | (h >> 19); h *= 0xC2B2AE35u;
    h ^= (uint32_t)b * 0x27D4EB2Fu; h ^= h >> 15; h *= 0x2C1B3C6Du;
    h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
    return h;
}
static float srnd(uint32_t seed, int a, int b) { return (hash3(a, b, seed) >> 8) * (1.0f / 16777216.0f); }
float world_rand(const World *w, int32_t a, int32_t b, uint32_t salt) {
    return srnd(w->seed + salt * 0x632BE5ABu, a, b);
}
static float vnoise(float x, float z, uint32_t seed) {
    int xi = (int)floorf(x), zi = (int)floorf(z);
    float fx = x - xi, fz = z - zi;
    fx = fx * fx * (3 - 2 * fx); fz = fz * fz * (3 - 2 * fz);
    float a = srnd(seed, xi, zi), b = srnd(seed, xi + 1, zi);
    float c = srnd(seed, xi, zi + 1), d = srnd(seed, xi + 1, zi + 1);
    return a + (b - a) * fx + (c - a) * fz + (a - b - c + d) * fx * fz;
}
static float fbm(float x, float z, uint32_t seed, int oct) {
    float s = 0, a = 0.5f, n = 0;
    for (int i = 0; i < oct; i++) {
        s += a * vnoise(x, z, seed + i * 131u); n += a; a *= 0.5f; x *= 2.03f; z *= 2.03f;
    }
    return s / n;
}
static float sstep(float a, float b, float x) {
    float t = (x - a) / (b - a); if (t < 0) t = 0; if (t > 1) t = 1; return t * t * (3 - 2 * t);
}

float world_height(const World *w, float x, float z) {
    uint32_t s = w->seed;
    // rolling lowlands + far ranges so the map never ends
    float base = fbm(x * 0.004f, z * 0.004f, s + 11, 4) * 30.0f;
    float far_r = fbm(x * 0.0006f + 7, z * 0.0006f - 3, s + 77, 3);
    far_r = far_r * far_r * far_r * 5.0f * 90.0f;
    far_r *= sstep(400.0f, 1200.0f, sqrtf(x * x + z * z));   // keep the spawn area gentle
    // main mountain, domain-warped so it is not a perfect cone
    float wx = x + (fbm(x * 0.003f, z * 0.003f, s + 5, 3) - 0.5f) * 900.0f;
    float wz = z + (fbm(x * 0.003f + 9, z * 0.003f + 9, s + 6, 3) - 0.5f) * 900.0f;
    float dx = wx - w->peakx, dz = wz - w->peakz;
    float m = 1.0f - sqrtf(dx * dx + dz * dz) / w->radius;
    if (m < 0) m = 0;
    float mm = m * m * (3 - 2 * m);
    float mtn = mm * 3000.0f;
    float rough = (fbm(x * 0.03f, z * 0.03f, s + 21, 3) - 0.5f) * 16.0f * mm;
    float h = base + far_r * (1.0f - mm) + mtn + rough;
    // terraces: flat ledges joined by steep cliff bands. Steeper higher up
    // because the underlying mountain gradient grows.
    float tb = (h - 8.0f) / 30.0f; if (tb < 0) tb = 0; if (tb > 1) tb = 1;
    float step = 16.0f + 10.0f * fbm(x * 0.002f, z * 0.002f, s + 33, 2);
    float t = h / step, fl = floorf(t), fr = t - fl;
    float terr = (fl + sstep(0.62f, 1.0f, fr)) * step;
    return h * (1 - tb) + terr * tb;
}

float world_slope(const World *w, float x, float z, float *gx, float *gz) {
    const float e = 0.75f;
    float dx = (world_height(w, x + e, z) - world_height(w, x - e, z)) / (2 * e);
    float dz = (world_height(w, x, z + e) - world_height(w, x, z - e)) / (2 * e);
    if (gx) *gx = dx;
    if (gz) *gz = dz;
    return sqrtf(dx * dx + dz * dz);
}

Biome world_biome(const World *w, float x, float z, float h) {
    float alt = h / w->sh;
    if (alt > 0.62f) return B_SNOWY;
    if (x * x + z * z < 150.0f * 150.0f) return B_RAINFOREST;
    float n = fbm(x * 0.0018f + 50, z * 0.0018f - 30, w->seed + 900, 3);
    if (alt > 0.40f && n > 0.5f) return B_SNOWY;
    if (n < 0.56f) return B_RAINFOREST;   // the "mostly rainforest" part
    if (n < 0.60f) return B_FOREST;
    if (n < 0.63f) return h < 25.0f ? B_SWAMP : B_FOREST;
    if (n < 0.66f) return B_DESERT;
    if (n < 0.69f) return B_MESA;
    return B_LAVA;
}

float world_grip(const World *w, float x, float z) {
    return 0.5f * vnoise(x * 0.22f, z * 0.22f, w->seed + 401) + 0.5f * vnoise(x * 0.7f, z * 0.7f, w->seed + 402);
}

float world_base_temp(const World *w, Biome b, float h) {
    static const float base[B_COUNT] = { 26, 30, 24, 16, -5, 36, 44 };
    (void)w;
    return base[b] - h * 0.0055f;
}

int world_loot(const World *w, int cx, int cz, float *x, float *z, uint32_t *id) {
    if (world_rand(w, cx, cz, 10) > 0.10f) return 0;
    *x = (cx + 0.1f + 0.8f * world_rand(w, cx, cz, 11)) * LOOT_CELL;
    *z = (cz + 0.1f + 0.8f * world_rand(w, cx, cz, 12)) * LOOT_CELL;
    *id = hash3(cx, cz, w->seed ^ 0xA5A5u);
    return 1;
}
int world_camp(const World *w, int cx, int cz, float *x, float *z) {
    if (world_rand(w, cx, cz, 20) > 0.65f) return 0;
    for (int i = 0; i < 8; i++) {
        float px = (cx + 0.1f + 0.8f * world_rand(w, cx, cz, 30 + i)) * CAMP_CELL;
        float pz = (cz + 0.1f + 0.8f * world_rand(w, cx, cz, 50 + i)) * CAMP_CELL;
        if (world_slope(w, px, pz, 0, 0) < 0.3f) { *x = px; *z = pz; return 1; }
    }
    return 0;
}

void world_init(World *w, uint32_t seed) {
    w->seed = seed;
    float ang = 6.2831853f * srnd(seed, 1, 2);
    w->radius = 2600.0f;
    w->peakx = cosf(ang) * w->radius * 1.25f;
    w->peakz = sinf(ang) * w->radius * 1.25f;
    // find the real summit
    w->sh = -1e9f;
    for (float zz = -400; zz <= 400; zz += 16)
        for (float xx = -400; xx <= 400; xx += 16) {
            float h = world_height(w, w->peakx + xx, w->peakz + zz);
            if (h > w->sh) { w->sh = h; w->sx = w->peakx + xx; w->sz = w->peakz + zz; }
        }
    w->ck[0][0] = 0; w->ck[0][2] = 0; w->ck[0][1] = world_height(w, 0, 0);
    for (int k = 1; k <= NUM_CHECKPOINTS; k++) {
        if (k == NUM_CHECKPOINTS) { w->ck[k][0] = w->sx; w->ck[k][1] = w->sh; w->ck[k][2] = w->sz; break; }
        float target = w->sh * k / NUM_CHECKPOINTS;
        float len = sqrtf(w->sx * w->sx + w->sz * w->sz);
        float dx = w->sx / len, dz = w->sz / len, px = 0, pz = 0;
        for (float d = 0; d < len; d += 4.0f) {
            px = dx * d; pz = dz * d;
            if (world_height(w, px, pz) >= target) break;
        }
        // nearest flat ledge so the checkpoint is somewhere you can stand
        float bx = px, bz = pz; int found = 0;
        for (float r = 0; r <= 60 && !found; r += 4)
            for (int a = 0; a < 12 && !found; a++) {
                float qx = px + cosf(a * 0.5236f) * r, qz = pz + sinf(a * 0.5236f) * r;
                float qh = world_height(w, qx, qz);
                if (world_slope(w, qx, qz, 0, 0) < 0.25f && qh > target - 3 && qh < target + 25) {
                    bx = qx; bz = qz; found = 1;
                }
            }
        w->ck[k][0] = bx; w->ck[k][2] = bz; w->ck[k][1] = world_height(w, bx, bz);
    }
}

float world_path(const World *w, float x, float z) {
    float best = 1e9f;
    for (int k = 0; k < NUM_CHECKPOINTS; k++) {
        float ax = w->ck[k][0], az = w->ck[k][2], bx = w->ck[k + 1][0], bz = w->ck[k + 1][2];
        float dx = bx - ax, dz = bz - az, l2 = dx * dx + dz * dz + 1e-6f, len = sqrtf(l2);
        float t = ((x - ax) * dx + (z - az) * dz) / l2;
        if (t < 0) t = 0;
        if (t > 1) t = 1;
        float off = sinf((t * len + k * 37.0f) * 0.03f) * 9.0f * sinf(t * 3.14159f);   // the trail winds
        float cx = ax + dx * t + (-dz / len) * off, cz = az + dz * t + (dx / len) * off;
        float d = sqrtf((x - cx) * (x - cx) + (z - cz) * (z - cz));
        if (d < best) best = d;
    }
    return best;
}

int world_lava_noise(const World *w, float x, float z) {
    if (world_path(w, x, z) < 6.0f) return 0;                       // the trail is always safe to walk
    return fbm(x * 0.025f, z * 0.025f, w->seed + 500, 3) > 0.60f;
}
int world_lava(const World *w, float x, float z) {
    float h = world_height(w, x, z);
    return world_biome(w, x, z, h) == B_LAVA && world_lava_noise(w, x, z);
}
