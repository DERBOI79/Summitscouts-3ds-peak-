// Summit Scouts - game logic (pure C, no 3DS dependencies, host-testable)
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "world.h"
#include "skin.h"

typedef enum {
    ITEM_CLOTH, ITEM_METAL, ITEM_FOOD, ITEM_BANDAGE,
    ITEM_ICE_PICK, ITEM_ICE_SCREW, ITEM_HOOK, ITEM_GRAPPLE,
    ITEM_WINDBREAKER, ITEM_JACKET, ITEM_PARKA, ITEM_COUNT
} Item;
extern const char *ITEM_NAME[ITEM_COUNT];

typedef enum { WX_CLEAR, WX_OVERCAST, WX_RAIN, WX_STORM, WX_THUNDER, WX_SUNSET, WX_COUNT } Weather;   // THUNDER = forced on the last 10% of the mountain
extern const char *WEATHER_NAME[WX_COUNT];
extern const char *BIOME_NAME[B_COUNT];
extern const char *CLOTHING_NAME[4];

typedef enum { PS_GROUND, PS_CLIMB, PS_FALL } PState;

enum { BTN_GRIP_L = 1, BTN_GRIP_R = 2, P_JUMP = 4, P_ANCHOR = 8, P_GRAPPLE = 16 };
typedef struct {
    float lx, ly;          // move stick, -1..1 (ly>0 = forward/up)
    float lookx, looky;    // radians this frame (positive lookx = turn left)
    uint32_t held;         // BTN_GRIP_*
    uint32_t pressed;      // P_* this frame
} Input;

typedef struct { float x, y, z; bool valid; } Anchor;

#define SAVE_MAGIC 0x53554D31u
#define LOOT_SET 2048
#define MAX_ANCHORS 4

typedef struct {
    World w;
    float x, y, z;               // feet position
    float yaw, pitch, vy, hvx, hvz;
    PState state;
    float fallStart, fallSx, fallSz, hang;
    float stamina, health, hunger, exposure, pump[2];
    bool  locked[2], gripL, gripR;
    float hookP[3];              // where your gear is attached (feet position at that moment)
    bool  hooked;                // clipped in to a hook / ice screw: you may hang with no hands and recover stamina
    int   inv[ITEM_COUNT];
    int   packTier, clothing, grappleUses;
    Anchor anchors[MAX_ANCHORS];
    double playtime;
    Weather wx, wxBase; int wxEpoch;   // wxBase = scheduled weather; wx = what you actually get (thunder overrides near the summit)
    float flash, nextFlash;            // lightning
    float rockTimer;                   // seconds until the next rockfall may be triggered
    bool  rockOn, rockHit;             // a boulder event is running / has already struck you
    float rockWarn;                    // seconds until impact (10 -> 0), keeps going negative while it rolls past
    float rockX, rockZ, rockUx, rockUz; // impact point on the slope and the uphill direction the boulder arrives from
    int   ckpt;                  // last checkpoint reached (0 = spawn)
    bool  won;
    uint32_t looted[LOOT_SET];
    float campCooldown;
    // derived each frame, for HUD / renderer
    float ambient, tolMin, tolMax, slopeHere, gripQ, staminaMax;
    Biome biome;
    float climbAnim;
    Skin  skin;                  // cosmetic look; main.c overwrites it after game_init
    char  msg[64]; float msgT;
} Game;

#define SUMMIT_STORM_FRAC 0.90f   // above this fraction of the summit height it is always a thunderstorm...
#define SUMMIT_SUNSET_FRAC 0.95f  // ...until you break through the clouds into a calm sunset
#define WEATHER_PERIOD 1200.0   // 20 minutes
#define ROCK_WARN_SECS 10.0f     // you always get this long to get out of the way
#define ROCK_SPEED 14.0f         // m/s down the slope
#define ROCK_RADIUS 1.8f
// current boulder position (for drawing). Returns false when no boulder is on the mountain.
bool game_rock_pos(const Game *g, float *x, float *y, float *z);
int  pack_capacity(const Game *g);
int  pack_used(const Game *g);

void game_init(Game *g, uint32_t seed);
void game_update(Game *g, float dt, const Input *in);
void game_say(Game *g, const char *s);

// Crafting / use (menu)
typedef struct { const char *name; Item out; int cloth, metal; } Recipe;
#define NUM_RECIPES 8
extern const Recipe RECIPES[NUM_RECIPES];   // last one is the pack upgrade
bool game_craft(Game *g, int recipe);
bool game_use(Game *g, Item it);            // eat / bandage / wear
bool game_upgrade_cost(const Game *g, int *cloth, int *metal);

bool game_looted(const Game *g, uint32_t id);
static inline uint32_t camp_crate_id(int cx, int cz) { return (uint32_t)(cx * 7919 + cz * 104729 + 1) | 0x80000000u; }

float game_hour(const Game *g);       // 0..24, starts at 06:00; 1 real minute = 1 game hour
float game_daylight(const Game *g);   // 0 = night, 1 = full day
float game_fog_far(const Game *g);          // metres
float game_time_to_next_weather(const Game *g);

bool game_save(const Game *g, const char *path);
bool game_load(Game *g, const char *path);
