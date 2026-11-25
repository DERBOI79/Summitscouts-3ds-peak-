#include "game.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

const char *ITEM_NAME[ITEM_COUNT] = { "Cloth", "Metal", "Food", "Bandage", "Ice pick", "Ice screw",
    "Hook", "Grapple", "Windbreaker", "Insul. jacket", "Parka" };
const char *WEATHER_NAME[WX_COUNT] = { "Clear", "Overcast", "Rain", "Storm", "Thunderstorm", "Sunset" };
const char *BIOME_NAME[B_COUNT] = { "Rainforest", "Mesa", "Swamp", "Forest", "Snowy", "Desert", "Lava" };
const char *CLOTHING_NAME[4] = { "T-shirt", "Windbreaker", "Insul. jacket", "Parka" };

// comfortable ambient range (deg C) per clothing level
static const float TOL_MIN[4] = { 15, 5, -8, -25 };
static const float TOL_MAX[4] = { 45, 38, 30, 22 };

const Recipe RECIPES[NUM_RECIPES] = {
    { "Ice pick",     ITEM_ICE_PICK,   1, 2 },
    { "Ice screw",    ITEM_ICE_SCREW,  0, 1 },
    { "Hook",         ITEM_HOOK,       1, 1 },
    { "Grapple",      ITEM_GRAPPLE,    2, 3 },
    { "Windbreaker",  ITEM_WINDBREAKER,3, 0 },
    { "Insul. jacket",ITEM_JACKET,     6, 0 },
    { "Parka",        ITEM_PARKA,      8, 1 },
    { "Pack upgrade", ITEM_COUNT,      0, 0 },
};
static const int PACK_CAP[6]   = { 15, 25, 35, 50, 60, 75 };
static const int UP_CLOTH[5]   = { 4, 6, 8, 10, 14 };
static const int UP_METAL[5]   = { 1, 2, 3, 4, 6 };

// ---------- tuning ----------
#define EYE_H        1.6f
#define WALK_SPEED   4.0f
#define CLIMB_SPEED  0.9f
#define WALK_SLOPE   1.0f    // steeper than 45 deg needs hands
#define GRIP_SLOPE   0.5f    // flatter than this you simply stand
#define GRAVITY      22.0f
#define GRAPPLE_GAIN 10.0f   // deliberately shorter than a typical 16-26 m cliff
// ----------------------------

int pack_capacity(const Game *g) { return PACK_CAP[g->packTier]; }
int pack_used(const Game *g) { int n = 0; for (int i = 0; i < ITEM_COUNT; i++) n += g->inv[i]; return n; }

void game_say(Game *g, const char *s) { snprintf(g->msg, sizeof g->msg, "%s", s); g->msgT = 3.0f; }

static int add_item(Game *g, Item it, int n) {
    int room = pack_capacity(g) - pack_used(g);
    if (n > room) n = room;
    if (n < 0) n = 0;
    g->inv[it] += n;
    if (it == ITEM_GRAPPLE && n > 0 && g->grappleUses <= 0) g->grappleUses = 5;
    return n;
}

static float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }

void game_init(Game *g, uint32_t seed) {
    memset(g, 0, sizeof *g);
    g->rockTimer = 150.0f;
    world_init(&g->w, seed);
    g->x = g->w.ck[0][0]; g->z = g->w.ck[0][2]; g->y = g->w.ck[0][1];
    // face the mountain
    float dx = g->w.sx - g->x, dz = g->w.sz - g->z;
    g->yaw = atan2f(-dx, -dz);
    g->state = PS_GROUND;
    g->stamina = g->staminaMax = 100; g->health = 100; g->hunger = 100;
    g->inv[ITEM_CLOTH] = 2; g->inv[ITEM_FOOD] = 2; g->inv[ITEM_METAL] = 1;
    g->wx = WX_CLEAR;
    skin_default(&g->skin);
    game_say(g, "Reach the flags. The summit is the last one.");
}

float game_fog_far(const Game *g) {
    static const float f[WX_COUNT] = { 95, 78, 58, 40, 30, 260 };   // heavy haze
    float far = f[g->wx];
    if (g->biome == B_RAINFOREST) far *= 0.85f;
    far *= 0.75f + 0.25f * game_daylight(g);
    return far;
}
float game_hour(const Game *g) { return fmodf(6.0f + (float)(g->playtime / 60.0), 24.0f); }
float game_daylight(const Game *g) {
    if (g->wx == WX_SUNSET) return 0.65f;      // the summit sunset is permanent: no night up there
    float d = sinf((game_hour(g) - 6.0f) / 24.0f * 6.2831853f);   // +1 at noon, -1 at midnight
    float v = 0.5f + d * 0.8f; return v < 0 ? 0 : (v > 1 ? 1 : v);
}
float game_time_to_next_weather(const Game *g) {
    return (float)((g->wxEpoch + 1) * WEATHER_PERIOD - g->playtime);
}

static void update_weather(Game *g) {
    int ep = (int)(g->playtime / WEATHER_PERIOD);
    bool zone = g->y >= SUMMIT_STORM_FRAC * g->w.sh;
    bool above = g->y >= SUMMIT_SUNSET_FRAC * g->w.sh;
    if (ep != g->wxEpoch || g->playtime <= 0) {
        g->wxEpoch = ep;
        float r = world_rand(&g->w, ep, 0, 99);
        Weather nw = ep == 0 ? WX_CLEAR : (r < 0.40f ? WX_CLEAR : r < 0.62f ? WX_OVERCAST : r < 0.90f ? WX_RAIN : WX_STORM);
        if (nw != g->wxBase) {
            g->wxBase = nw;
            if (!zone && !above) { char b[64]; snprintf(b, sizeof b, "Weather: %s", WEATHER_NAME[nw]); game_say(g, b); }
        }
    }
    Weather want = above ? WX_SUNSET : (zone ? WX_THUNDER : g->wxBase);
    if (want != g->wx) {
        if (want == WX_SUNSET) game_say(g, "You rose above the clouds. Almost there!");
        else if (want == WX_THUNDER) { game_say(g, g->wx == WX_SUNSET ? "Back down into the storm." : "Thunderstorm! The rock is soaked and slick."); g->nextFlash = 1.0f; }
        else if (g->wx == WX_THUNDER || g->wx == WX_SUNSET) game_say(g, "You climb below the storm.");
        g->wx = want;
    }
}

static void respawn(Game *g) {
    int k = g->ckpt;
    g->x = g->w.ck[k][0]; g->z = g->w.ck[k][2]; g->y = g->w.ck[k][1];
    g->state = PS_GROUND; g->vy = g->hvx = g->hvz = 0;
    g->health = 70; g->stamina = 100; g->exposure = 0; if (g->hunger < 40) g->hunger = 40;
    g->pump[0] = g->pump[1] = 0; g->locked[0] = g->locked[1] = false;
    g->hooked = false; g->rockOn = false; g->rockWarn = 0; g->rockTimer = 90.0f;
    for (int i = 0; i < MAX_ANCHORS; i++) g->anchors[i].valid = false;
    game_say(g, "You passed out. Back at the last checkpoint.");
}

static void start_fall(Game *g, float hx, float hz, float vy) {
    g->hooked = false;
    g->state = PS_FALL; g->vy = vy; g->hvx = hx; g->hvz = hz;
    g->fallStart = g->y; g->fallSx = g->x; g->fallSz = g->z;
}

static bool looted(const Game *g, uint32_t id) {
    uint32_t i = id % LOOT_SET;
    for (int n = 0; n < LOOT_SET; n++, i = (i + 1) % LOOT_SET) {
        if (g->looted[i] == 0) return false;
        if (g->looted[i] == id) return true;
    }
    return true;
}
bool game_looted(const Game *g, uint32_t id) { return looted(g, id); }
static void mark_looted(Game *g, uint32_t id) {
    uint32_t i = id % LOOT_SET;
    for (int n = 0; n < LOOT_SET; n++, i = (i + 1) % LOOT_SET)
        if (g->looted[i] == 0 || g->looted[i] == id) { g->looted[i] = id; return; }
}

static void give_loot(Game *g, uint32_t id, bool crate) {
    float r = (id >> 8 & 0xFFFF) / 65536.0f;
    int got = 0; char b[64];
    if (crate) { got += add_item(g, ITEM_CLOTH, 2) + add_item(g, ITEM_METAL, 1) + add_item(g, ITEM_FOOD, 1); }
    Item it = r < 0.34f ? ITEM_CLOTH : r < 0.54f ? ITEM_METAL : r < 0.72f ? ITEM_FOOD :
              r < 0.80f ? ITEM_BANDAGE : r < 0.88f ? ITEM_HOOK : r < 0.96f ? ITEM_ICE_SCREW : ITEM_ICE_PICK;
    int n = (it == ITEM_CLOTH || it == ITEM_METAL) ? 2 : 1;
    got += add_item(g, it, n);
    if (got == 0) { game_say(g, "Pack full!"); return; }
    if (crate) snprintf(b, sizeof b, "Camp crate: %d items", got);
    else snprintf(b, sizeof b, "Found %s x%d", ITEM_NAME[it], got);
    game_say(g, b);
}

// A button on foot: loot, camp crate / rest. returns true if it consumed the press
static bool interact(Game *g) {
    for (int cz = (int)floorf(g->z / CAMP_CELL) - 1; cz <= (int)floorf(g->z / CAMP_CELL) + 1; cz++)
        for (int cx = (int)floorf(g->x / CAMP_CELL) - 1; cx <= (int)floorf(g->x / CAMP_CELL) + 1; cx++) {
            float px, pz;
            if (!world_camp(&g->w, cx, cz, &px, &pz)) continue;
            if (hypotf(px - g->x, pz - g->z) > 6.0f) continue;
            uint32_t id = camp_crate_id(cx, cz);
            if (!looted(g, id)) { mark_looted(g, id); give_loot(g, id * 2654435761u, true); return true; }
            if (g->campCooldown <= 0) {
                g->stamina = g->staminaMax; g->exposure = 0; g->health = clampf(g->health + 25, 0, 100);
                g->campCooldown = 90; game_say(g, "Rested by the campfire."); return true;
            }
            game_say(g, "The fire is still warm. Not now."); return true;
        }
    for (int cz = (int)floorf(g->z / LOOT_CELL) - 1; cz <= (int)floorf(g->z / LOOT_CELL) + 1; cz++)
        for (int cx = (int)floorf(g->x / LOOT_CELL) - 1; cx <= (int)floorf(g->x / LOOT_CELL) + 1; cx++) {
            float px, pz; uint32_t id;
            if (!world_loot(&g->w, cx, cz, &px, &pz, &id) || looted(g, id)) continue;
            if (hypotf(px - g->x, pz - g->z) > 3.0f) continue;
            mark_looted(g, id); give_loot(g, id, false); return true;
        }
    return false;
}

static void tolerance(Game *g) {
    float t = world_base_temp(&g->w, g->biome, g->y);
    static const float wxmod[WX_COUNT] = { 0, -2, -4, -7, -9, -3 };
    g->ambient = t + wxmod[g->wx] - 10.0f * (1.0f - game_daylight(g));   // nights are cold
    g->tolMin = TOL_MIN[g->clothing]; g->tolMax = TOL_MAX[g->clothing];
}

static void try_catch(Game *g) {
    float dist = g->fallStart - g->y;
    if (dist < 6.0f || g->vy > -8.0f) return;
    // placed ice screw: pulled back to the anchor
    int best = -1;
    for (int i = 0; i < MAX_ANCHORS; i++)
        if (g->anchors[i].valid && g->anchors[i].y <= g->fallStart + 0.5f && g->anchors[i].y > g->y)
            if (best < 0 || g->anchors[i].y < g->anchors[best].y) best = i;
    if (best >= 0) {
        g->x = g->anchors[best].x; g->y = g->anchors[best].y; g->z = g->anchors[best].z;
        g->anchors[best].valid = false;
        g->state = PS_CLIMB; g->hang = 1.5f; g->vy = 0; g->hookP[0] = g->x; g->hookP[1] = g->y; g->hookP[2] = g->z; g->hooked = true; game_say(g, "The ice screw held! Let go to rest."); return;
    }
    if (g->inv[ITEM_HOOK] > 0) {
        for (int i = 1; i <= 40; i++) {
            float f = i / 40.0f, px = g->x + (g->fallSx - g->x) * f, pz = g->z + (g->fallSz - g->z) * f;
            float h = world_height(&g->w, px, pz);
            if (h >= g->y - 0.5f && world_slope(&g->w, px, pz, 0, 0) >= GRIP_SLOPE) {
                g->x = px; g->z = pz; g->y = h; g->inv[ITEM_HOOK]--;
                g->state = PS_CLIMB; g->hang = 1.5f; g->vy = 0; g->hookP[0] = g->x; g->hookP[1] = g->y; g->hookP[2] = g->z; g->hooked = true; game_say(g, "Your hook caught the wall! Let go to rest."); return;
            }
        }
    }
}

static void use_grapple(Game *g) {
    if (g->inv[ITEM_GRAPPLE] <= 0) { game_say(g, "No grapple."); return; }
    if (g->state == PS_FALL) return;
    if (g->stamina < 20) { game_say(g, "Too tired to throw."); return; }
    float fx = -sinf(g->yaw), fz = -cosf(g->yaw);
    for (float d = 1.0f; d < 14.0f; d += 0.5f) {
        float px = g->x + fx * d, pz = g->z + fz * d, h = world_height(&g->w, px, pz);
        if (h > g->y + 1.5f) {
            float gain = h - g->y; if (gain > GRAPPLE_GAIN) gain = GRAPPLE_GAIN;
            // find the surface point at the clamped height along the same line
            for (float e = 1.0f; e <= d; e += 0.25f) {
                float qx = g->x + fx * e, qz = g->z + fz * e;
                if (world_height(&g->w, qx, qz) >= g->y + gain) { px = qx; pz = qz; break; }
            }
            g->x = px; g->z = pz; g->y = world_height(&g->w, px, pz);
            g->stamina -= 15; g->state = PS_CLIMB; g->hang = 1.2f;
            if (--g->grappleUses <= 0) { g->inv[ITEM_GRAPPLE]--; g->grappleUses = g->inv[ITEM_GRAPPLE] > 0 ? 5 : 0; game_say(g, "Grapple broke."); }
            else game_say(g, "Grapple!");
            return;
        }
    }
    game_say(g, "Nothing to hook onto.");
}


bool game_rock_pos(const Game *g, float *x, float *y, float *z) {
    if (!g->rockOn) return false;
    float s = g->rockWarn * ROCK_SPEED;              // metres uphill of the impact point (negative = already past it)
    *x = g->rockX + g->rockUx * s; *z = g->rockZ + g->rockUz * s;
    *y = world_height(&g->w, *x, *z) + ROCK_RADIUS * 0.8f;
    return true;
}

// Rockfall: only while you are on a steep face. A boulder is announced ROCK_WARN_SECS ahead, aimed at where you are
// at that moment, then rolls down the fall line. Moving ~4 m sideways is enough, and 10 s is plenty of time to do it.
static void rock_update(Game *g, float dt, float gx, float gz) {
    World *w = &g->w;
    if (!g->rockOn) {
        bool ok = g->state != PS_FALL && g->slopeHere > 0.7f && g->y > 40.0f && g->wx != WX_SUNSET;
        if (ok) g->rockTimer -= dt;
        if (g->rockTimer > 0) return;
        float l = hypotf(gx, gz);
        if (l < 1e-3f) { g->rockTimer = 10.0f; return; }
        g->rockOn = true; g->rockHit = false; g->rockWarn = ROCK_WARN_SECS;
        g->rockX = g->x; g->rockZ = g->z; g->rockUx = gx / l; g->rockUz = gz / l;     // gradient points uphill
        game_say(g, "ROCKFALL! Boulder in 10s - move sideways!");
        return;
    }
    int before = (int)ceilf(g->rockWarn);
    g->rockWarn -= dt;
    int after = (int)ceilf(g->rockWarn);
    if (g->rockWarn > 0 && after != before && after <= 5 && after >= 1) { char b[64]; snprintf(b, sizeof b, "Boulder in %ds!", after); game_say(g, b); }
    float bx, by, bz;
    game_rock_pos(g, &bx, &by, &bz);
    if (!g->rockHit && hypotf(bx - g->x, bz - g->z) < ROCK_RADIUS + 1.4f && fabsf(by - g->y) < 4.0f) {
        g->rockHit = true;
        g->health -= 35.0f;
        g->locked[0] = g->locked[1] = true; g->pump[0] = g->pump[1] = 100;       // knocked off the wall
        game_say(g, "A boulder hit you! (-35 HP)");
        start_fall(g, -g->rockUx * 5.0f, -g->rockUz * 5.0f, 2.0f);
    } else if (!g->rockHit && g->rockWarn < 0 && g->rockWarn > -0.2f ) game_say(g, "It missed you.");
    if (g->rockWarn < -6.0f) { g->rockOn = false; g->rockTimer = 150.0f + 150.0f * world_rand(w, (int)g->playtime, 9, 321); }
}

void game_update(Game *g, float dt, const Input *in) {
    World *w = &g->w;
    if (g->won) return;
    g->playtime += dt;
    if (g->msgT > 0) g->msgT -= dt;
    if (g->campCooldown > 0) g->campCooldown -= dt;
    update_weather(g);
    if (g->flash > 0) g->flash -= dt;
    if (g->wx == WX_THUNDER) {
        g->nextFlash -= dt;
        if (g->nextFlash <= 0) { g->flash = 0.18f; g->nextFlash = 3.0f + 6.0f * world_rand(&g->w, (int)(g->playtime * 10), 7, 123); }
    }

    g->yaw += in->lookx;
    g->pitch = clampf(g->pitch + in->looky, -1.4f, 1.4f);

    float hh = world_height(w, g->x, g->z);
    g->biome = world_biome(w, g->x, g->z, hh);
    float gx, gz; g->slopeHere = world_slope(w, g->x, g->z, &gx, &gz);
    g->gripQ = world_grip(w, g->x, g->z);
    tolerance(g);
    rock_update(g, dt, gx, gz);

    // ----- survival -----
    g->hunger = clampf(g->hunger - 0.028f * dt, 0, 100);
    if (g->hunger <= 0) g->health -= 0.4f * dt;
    g->staminaMax = 100 - (g->health < 50 ? 20 : 0) - (g->hunger < 20 ? 30 : 0);
    float margin = 0;
    if (g->ambient < g->tolMin) margin = g->tolMin - g->ambient;
    else if (g->ambient > g->tolMax) margin = g->ambient - g->tolMax;
    if (margin > 0) g->exposure += clampf(margin * 0.3f, 0.5f, 6.0f) * dt;
    else g->exposure = clampf(g->exposure - 4.0f * dt, 0, 100);
    if (g->exposure >= 100) { respawn(g); return; }
    if (g->health <= 0) { respawn(g); return; }
    if (g->hunger > 30 && margin <= 0 && g->health < 100) g->health = clampf(g->health + 0.3f * dt, 0, 100);

    // ----- hands -----
    bool wantL = (in->held & BTN_GRIP_L) != 0, wantR = (in->held & BTN_GRIP_R) != 0;
    if (g->hang > 0) { g->hang -= dt; wantL = wantR = true; }
    g->gripL = wantL && !g->locked[0]; g->gripR = wantR && !g->locked[1];
    float wxMul = g->wx == WX_RAIN ? 1.25f : g->wx == WX_STORM ? 1.5f : g->wx == WX_THUNDER ? 2.2f : 1.0f;   // wet rock: stamina drains faster and hands slip sooner
    bool slick = g->gripQ < 0.30f;
    bool icy = g->biome == B_SNOWY;
    float pumpMul = wxMul * (slick ? 3.0f : 1.0f) * (icy && g->inv[ITEM_ICE_PICK] <= 0 ? 1.8f : 1.0f);
    if (g->state == PS_CLIMB) {
        bool hold[2] = { g->gripL, g->gripR };
        int c = hold[0] + hold[1];
        for (int i = 0; i < 2; i++) {
            if (hold[i]) g->pump[i] += (c == 1 ? 12.0f : 4.0f) * pumpMul * dt;
            else g->pump[i] -= 15.0f * dt;
            g->pump[i] = clampf(g->pump[i], 0, 100);
            if (g->pump[i] >= 100) { g->locked[i] = true; game_say(g, i ? "Right hand slipped!" : "Left hand slipped!"); }
            if (g->locked[i] && g->pump[i] < 35) g->locked[i] = false;
        }
        g->gripL = wantL && !g->locked[0]; g->gripR = wantR && !g->locked[1];
    } else {
        for (int i = 0; i < 2; i++) { g->pump[i] = clampf(g->pump[i] - 25.0f * dt, 0, 100); if (g->pump[i] < 35) g->locked[i] = false; }
    }
    int grips = g->gripL + g->gripR;

    // ----- stamina -----
    float injured = g->health < 50 ? 1.3f : 1.0f;
    if (g->state != PS_CLIMB) g->hooked = false;
    if (g->state == PS_CLIMB) {
        bool moving = grips == 1 && g->stamina > 0 && (fabsf(in->lx) > 0.15f || fabsf(in->ly) > 0.15f);
        if (g->hooked && grips == 0) {
            g->stamina += (g->hunger > 0 ? 6.0f : 2.0f) * dt;        // hanging from your gear with no hands: the only way to recover on a wall
        } else {
            float d = grips >= 2 ? 0.0f : (moving ? 3.0f : 1.5f);    // two hands just hold you: no drain, no recovery
            if (icy && g->inv[ITEM_ICE_PICK] > 0) d *= 0.7f;
            if (g->hang <= 0) g->stamina -= d * wxMul * injured * dt;
        }
    } else if (g->state == PS_GROUND) {
        g->stamina += (g->hunger > 0 ? 14.0f : 4.0f) * dt;
    }
    g->stamina = clampf(g->stamina, -5, g->staminaMax);
    float climbSpeedMul = 1.0f / injured;

    // ----- abilities -----
    if (in->pressed & P_GRAPPLE) use_grapple(g);
    if ((in->pressed & P_ANCHOR) && g->state == PS_CLIMB) {
        if (g->inv[ITEM_ICE_SCREW] > 0) {
            for (int i = MAX_ANCHORS - 1; i > 0; i--) g->anchors[i] = g->anchors[i - 1];
            g->anchors[0] = (Anchor){ g->x, g->y, g->z, true };
            g->inv[ITEM_ICE_SCREW]--; g->hookP[0] = g->x; g->hookP[1] = g->y; g->hookP[2] = g->z; g->hooked = true; game_say(g, "Clipped in! Let go with both hands to rest.");
        } else game_say(g, "No ice screws.");
    }

    float fx = -sinf(g->yaw), fz = -cosf(g->yaw), rx = cosf(g->yaw), rz = -sinf(g->yaw);

    switch (g->state) {
    case PS_GROUND: {
        if ((in->pressed & P_JUMP) && !interact(g) && g->stamina > 8) {
            g->stamina -= 6;
            start_fall(g, (fx * in->ly + rx * in->lx) * WALK_SPEED, (fz * in->ly + rz * in->lx) * WALK_SPEED, 6.5f);
            break;
        }
        float sp = WALK_SPEED * (g->hunger < 20 ? 0.8f : 1.0f);
        float nx = g->x + (fx * in->ly + rx * in->lx) * sp * dt;
        float nz = g->z + (fz * in->ly + rz * in->lx) * sp * dt;
        float nh = world_height(w, nx, nz), ns = world_slope(w, nx, nz, 0, 0);
        if (ns <= WALK_SLOPE && fabsf(nh - g->y) <= 1.2f + 6.0f * dt) { g->x = nx; g->z = nz; g->y = nh; }
        else if (grips > 0 && ns >= GRIP_SLOPE && g->stamina > 5) { g->x = nx; g->z = nz; g->y = nh; g->state = PS_CLIMB; }
        else if (nh < g->y - 1.2f) { g->x = nx; g->z = nz; start_fall(g, (nx - g->x) * 0 + (fx * in->ly + rx * in->lx) * 2.0f, (fz * in->ly + rz * in->lx) * 2.0f, 0); }
        else { g->y = hh; } // blocked by a wall: you need your hands
        break; }

    case PS_CLIMB: {
        if (g->hooked && (grips == 0 || g->stamina <= 0)) {          // dangling from the gear
            g->y = hh;
            if (grips > 0 && g->msgT <= 0) game_say(g, "Too tired - let go and hang to rest.");
            break;
        }
        if (grips == 0 || g->stamina <= 0) {
            if (g->slopeHere <= WALK_SLOPE && g->stamina > 0) { g->state = PS_GROUND; break; }
            float l = hypotf(gx, gz) + 1e-4f;
            start_fall(g, -gx / l * 2.5f, -gz / l * 2.5f, 0);
            if (g->stamina <= 0) game_say(g, "Out of stamina!");
            break;
        }
        if (g->slopeHere < GRIP_SLOPE) { g->state = PS_GROUND; game_say(g, "Pulled up onto the ledge."); break; }
        if (grips == 1 && (fabsf(in->lx) > 0.15f || fabsf(in->ly) > 0.15f)) {
            g->hooked = false;                                          // you climbed off your gear
            float l = hypotf(gx, gz) + 1e-4f, ux = gx / l, uz = gz / l; // uphill
            float cx = -uz, cz = ux;                                    // contour
            if (cx * rx + cz * rz < 0) { cx = -cx; cz = -cz; }
            float vs = CLIMB_SPEED * climbSpeedMul * (g->gripQ < 0.30f ? 0.6f : 1.0f);
            float hz = vs * dt / sqrtf(1.0f + g->slopeHere * g->slopeHere);
            g->x += ux * in->ly * hz + cx * in->lx * vs * dt;
            g->z += uz * in->ly * hz + cz * in->lx * vs * dt;
            g->y = world_height(w, g->x, g->z);
            float ty = atan2f(-ux, -uz), dy = ty - g->yaw;
            while (dy > 3.14159f) dy -= 6.28318f;
            while (dy < -3.14159f) dy += 6.28318f;
            g->yaw += dy * clampf(3.0f * dt, 0, 1);
            g->climbAnim += dt * 2.5f;
        } else g->y = hh;
        break; }

    case PS_FALL: {
        g->vy -= GRAVITY * dt;
        g->y += g->vy * dt; g->x += g->hvx * dt; g->z += g->hvz * dt;
        if (g->y > g->fallStart) g->fallStart = g->y;
        try_catch(g);
        if (g->state != PS_FALL) break;
        float h = world_height(w, g->x, g->z);
        if (g->y <= h) {
            float s = world_slope(w, g->x, g->z, &gx, &gz);
            if (s > WALK_SLOPE) {
                if (grips > 0 && g->vy > -7.0f && g->stamina > 10 && s >= GRIP_SLOPE) {
                    g->y = h; g->state = PS_CLIMB; g->vy = 0; g->hang = 0.6f; game_say(g, "Grabbed on!");
                } else {
                    float l = hypotf(gx, gz) + 1e-4f;
                    g->y = h; g->vy = 0; g->hvx = -gx / l * 4.5f; g->hvz = -gz / l * 4.5f; // tumble down the face
                }
            } else {
                float dist = g->fallStart - h;
                float dmg = dist > 8.0f ? (dist - 8.0f) * 3.0f : 0;
                g->y = h; g->vy = g->hvx = g->hvz = 0; g->state = PS_GROUND;
                if (dmg > 0) { g->health -= dmg; char b[64]; snprintf(b, sizeof b, "Ouch! Fell %.0f m (-%.0f HP)", dist, dmg); game_say(g, b); }
            }
        }
        break; }
    }

    // ----- lava burns (only pools on flat ground; cliffs and the trail are safe) -----
    if (g->state == PS_GROUND && g->biome == B_LAVA && world_lava_noise(w, g->x, g->z) && g->slopeHere < 0.6f) {
        g->health -= 20.0f * dt; g->exposure += 10.0f * dt;
        if (g->msgT <= 0) game_say(g, "LAVA! Get out!");
    }

    // ----- checkpoints -----
    if (g->ckpt < NUM_CHECKPOINTS) {
        const float *c = w->ck[g->ckpt + 1];
        if (hypotf(c[0] - g->x, c[2] - g->z) < 6.0f && fabsf(c[1] - g->y) < 6.0f) {
            g->ckpt++;
            char b[64];
            if (g->ckpt == NUM_CHECKPOINTS) { g->won = true; snprintf(b, sizeof b, "SUMMIT REACHED!"); }
            else snprintf(b, sizeof b, "Checkpoint %d/%d reached!", g->ckpt, NUM_CHECKPOINTS);
            game_say(g, b);
        }
    }
}

bool game_upgrade_cost(const Game *g, int *cloth, int *metal) {
    if (g->packTier >= 5) return false;
    *cloth = UP_CLOTH[g->packTier]; *metal = UP_METAL[g->packTier]; return true;
}

bool game_craft(Game *g, int r) {
    if (r < 0 || r >= NUM_RECIPES) return false;
    if (r == NUM_RECIPES - 1) {
        int c, m;
        if (!game_upgrade_cost(g, &c, &m)) { game_say(g, "Pack is maxed out."); return false; }
        if (g->inv[ITEM_CLOTH] < c || g->inv[ITEM_METAL] < m) { game_say(g, "Not enough materials."); return false; }
        g->inv[ITEM_CLOTH] -= c; g->inv[ITEM_METAL] -= m; g->packTier++;
        char b[64]; snprintf(b, sizeof b, "Pack now holds %d items", pack_capacity(g)); game_say(g, b); return true;
    }
    const Recipe *rc = &RECIPES[r];
    if (g->inv[ITEM_CLOTH] < rc->cloth || g->inv[ITEM_METAL] < rc->metal) { game_say(g, "Not enough materials."); return false; }
    int freed = rc->cloth + rc->metal;
    if (pack_used(g) - freed + 1 > pack_capacity(g)) { game_say(g, "Pack full."); return false; }
    g->inv[ITEM_CLOTH] -= rc->cloth; g->inv[ITEM_METAL] -= rc->metal;
    add_item(g, rc->out, 1);
    char b[64]; snprintf(b, sizeof b, "Crafted %s", rc->name); game_say(g, b);
    return true;
}

bool game_use(Game *g, Item it) {
    if (g->inv[it] <= 0) return false;
    switch (it) {
    case ITEM_FOOD: g->hunger = clampf(g->hunger + 40, 0, 100); g->inv[it]--; game_say(g, "Ate some food."); return true;
    case ITEM_BANDAGE: g->health = clampf(g->health + 40, 0, 100); g->inv[it]--; game_say(g, "Patched up."); return true;
    case ITEM_WINDBREAKER: case ITEM_JACKET: case ITEM_PARKA: {
        int lvl = it == ITEM_WINDBREAKER ? 1 : it == ITEM_JACKET ? 2 : 3;
        g->inv[it]--;
        if (g->clothing > 0) g->inv[ITEM_WINDBREAKER + g->clothing - 1]++; // old garment back in pack
        g->clothing = lvl; game_say(g, "Changed clothes."); return true; }
    default: return false;
    }
}

bool game_save(const Game *g, const char *path) {
    FILE *f = fopen(path, "wb"); if (!f) return false;
    uint32_t hdr[3] = { SAVE_MAGIC, g->w.seed, (uint32_t)g->ckpt };
    fwrite(hdr, sizeof hdr, 1, f);
    fwrite(&g->playtime, sizeof g->playtime, 1, f);
    fwrite(g->inv, sizeof g->inv, 1, f);
    fwrite(&g->packTier, sizeof g->packTier, 1, f);
    fwrite(&g->clothing, sizeof g->clothing, 1, f);
    fwrite(&g->grappleUses, sizeof g->grappleUses, 1, f);
    fwrite(g->looted, sizeof g->looted, 1, f);
    fclose(f); return true;
}
bool game_load(Game *g, const char *path) {
    FILE *f = fopen(path, "rb"); if (!f) return false;
    uint32_t hdr[3];
    if (fread(hdr, sizeof hdr, 1, f) != 1 || hdr[0] != SAVE_MAGIC) { fclose(f); return false; }
    game_init(g, hdr[1]);
    g->ckpt = (int)hdr[2];
    bool ok = fread(&g->playtime, sizeof g->playtime, 1, f) == 1 &&
              fread(g->inv, sizeof g->inv, 1, f) == 1 &&
              fread(&g->packTier, sizeof g->packTier, 1, f) == 1 &&
              fread(&g->clothing, sizeof g->clothing, 1, f) == 1 &&
              fread(&g->grappleUses, sizeof g->grappleUses, 1, f) == 1 &&
              fread(g->looted, sizeof g->looted, 1, f) == 1;
    fclose(f);
    if (!ok) return false;
    g->wxEpoch = -1;   // force the weather to be recomputed for the loaded time
    respawn(g); game_say(g, "Save loaded.");
    return true;
}
