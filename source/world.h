// Summit Scouts - world generation (pure C, no 3DS dependencies, host-testable)
#pragma once
#include <stdint.h>

typedef enum { B_RAINFOREST, B_MESA, B_SWAMP, B_FOREST, B_SNOWY, B_DESERT, B_LAVA, B_COUNT } Biome;

#define NUM_CHECKPOINTS 12   // checkpoint 12 is the summit

typedef struct {
    uint32_t seed;
    float peakx, peakz;      // centre of the main mountain
    float radius;            // mountain footprint
    float sx, sz, sh;        // true summit position / height
    float ck[NUM_CHECKPOINTS + 1][3]; // [0]=spawn, [1..12]=checkpoints
} World;

void  world_init(World *w, uint32_t seed);
float world_height(const World *w, float x, float z);
// tangent of slope angle (1.0 = 45 deg). gx,gz = uphill gradient (may be NULL)
float world_slope(const World *w, float x, float z, float *gx, float *gz);
Biome world_biome(const World *w, float x, float z, float h);
// Lava pools: only in the lava biome, never on the trail. world_lava_noise skips the biome test (caller already knows).
int world_lava_noise(const World *w, float x, float z);
int world_lava(const World *w, float x, float z);
float world_grip(const World *w, float x, float z);   // 0..1, <0.3 = slick
float world_rand(const World *w, int32_t a, int32_t b, uint32_t salt); // [0,1)

// Points of interest, cell based so the map is infinite and deterministic.
#define LOOT_CELL 24
#define CAMP_CELL 160
int world_loot(const World *w, int cx, int cz, float *x, float *z, uint32_t *id);
int world_camp(const World *w, int cx, int cz, float *x, float *z);

// Distance (m) to the winding trail that links spawn -> every checkpoint -> summit
float world_path(const World *w, float x, float z);

// Temperature in deg C at a location, before weather
float world_base_temp(const World *w, Biome b, float h);
