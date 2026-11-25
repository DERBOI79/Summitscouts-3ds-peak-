// Summit Scouts - 3DS entry point: input, title/seed screen, HUD + crafting menu on the bottom screen.
#include <3ds.h>
#include <citro3d.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "game.h"
#include "render.h"
#include "skin.h"

#define SAVE_PATH "sdmc:/3ds/summit_scouts.sav"

#define SKIN_PATH "sdmc:/3ds/summit_scouts_skin.bin"

static Game game;
static Skin mySkin;

static void bar(char *out, float v, float mx) {
    int n = (int)(10.0f * (v < 0 ? 0 : v) / mx + 0.5f); if (n > 10) n = 10;
    int i = 0; out[i++] = '['; for (; i <= 10; i++) out[i] = (i - 1 < n) ? '#' : '.'; out[i++] = ']'; out[i] = 0;
}

static const char *compass(const Game *g, float *dist) {
    const float *t = g->w.ck[g->ckpt + 1 > NUM_CHECKPOINTS ? NUM_CHECKPOINTS : g->ckpt + 1];
    float dx = t[0] - g->x, dz = t[2] - g->z; *dist = hypotf(dx, dz);
    float want = atan2f(-dx, -dz), d = want - g->yaw;
    while (d > 3.14159f) d -= 6.28318f;
    while (d < -3.14159f) d += 6.28318f;
    if (fabsf(d) < 0.4f) return "AHEAD";
    if (fabsf(d) > 2.4f) return "BEHIND";
    return d > 0 ? "<- LEFT" : "RIGHT ->";
}

static void draw_title(uint32_t seed) {
    printf("\x1b[2J\x1b[1;1H");
    printf("  S U M M I T   S C O U T S\n  ---------------------------\n\n");
    printf("  World seed: %-12lu\n\n", (unsigned long)seed);
    printf("  Up/Down    seed +/- 1\n  Left/Right seed +/- 1000\n  X          random seed\n  A          START NEW EXPEDITION\n  Y          load save\n  SELECT     CUSTOMIZE CHARACTER\n  START      quit\n");
}


// ---------------- character customiser ----------------
// Rows: 0..7 colours, 8..12 outfit pieces, 13 preset looks.
#define ROW_PIECE0 SC_COUNT
#define ROW_PRESET (SC_COUNT + SP_COUNT)
#define NUM_ROWS (ROW_PRESET + 1)
#define BAR_COL0 3
#define BAR_W 28
#define SLIDER_ROW0 20            // 1-based console rows of the R, G, B sliders

static void slider_text(char *out, int v) {
    int n = (v * BAR_W + 127) / 255;
    for (int i = 0; i < BAR_W; i++) out[i] = i < n ? '#' : '.';
    out[BAR_W] = 0;
}

static void draw_custom(const Skin *s, int sel, int preset) {
    char hx[8], bar[BAR_W + 1];
    printf("\x1b[1;1H CHARACTER CUSTOMIZER        L/R: turn  ");
    for (int i = 0; i < SC_COUNT; i++) { skin_hex(s->rgb[i], hx); printf("\x1b[%d;1H%c %-7s %s            ", 3 + i, sel == i ? '>' : ' ', SLOT_NAME[i], hx); }
    for (int p = 0; p < SP_COUNT; p++)
        printf("\x1b[%d;1H%c %-7s < %-16s >   ", 3 + ROW_PIECE0 + p, sel == ROW_PIECE0 + p ? '>' : ' ', PIECE_CAT[p], PIECE_NAME[p][s->piece[p]]);
    printf("\x1b[%d;1H%c %-7s < look %d of %d >         ", 3 + ROW_PRESET, sel == ROW_PRESET ? '>' : ' ', "Preset", preset + 1, SKIN_PRESETS);
    static const char *ch = "RGB";
    for (int c = 0; c < 3; c++) {
        if (sel < SC_COUNT) { slider_text(bar, s->rgb[sel][c]); printf("\x1b[%d;1H%c [%s] %3d  ", SLIDER_ROW0 + c, ch[c], bar, s->rgb[sel][c]); }
        else printf("\x1b[%d;1H%-40s", SLIDER_ROW0 + c, "");
    }
    printf("\x1b[24;1H%s", sel < SC_COUNT ? "A: type hex code   X: random   Touch: sliders" : "Left/Right: change piece / look           ");
    printf("\x1b[26;1HUp/Down select   B: save and back          ");
}

static void edit_hex(Skin *s, int slot) {
    SwkbdState kb; char buf[16] = "", cur[8];
    skin_hex(s->rgb[slot], cur);
    swkbdInit(&kb, SWKBD_TYPE_NORMAL, 2, 7);
    swkbdSetHintText(&kb, "Hex color, e.g. FF8800");
    swkbdSetInitialText(&kb, cur + 1);
    if (swkbdInputText(&kb, buf, sizeof buf) == SWKBD_BUTTON_CONFIRM) {
        uint8_t c[3]; if (skin_parse_hex(buf, c)) memcpy(s->rgb[slot], c, 3);
    }
}

static void draw_hud(const Game *g, bool menu, int sel) {
    char b1[16], b2[16], b3[16], b4[16];
    printf("\x1b[1;1H");
    if (!menu) {
        float dist; const char *dir = compass(g, &dist);
        bar(b1, g->stamina, 100); bar(b2, g->health, 100); bar(b3, g->hunger, 100); bar(b4, g->exposure, 100);
        int mins = (int)(g->playtime / 60);
        printf("%-40s\n", g->msgT > 0 ? g->msg : "");
        int hr = (int)game_hour(g);
        printf("Clock %02d:%02d %-5s Ckpt %2d/%d  run %d:%02d\n", hr, (int)((game_hour(g) - hr) * 60), game_daylight(g) > 0.5f ? "day" : "night", g->ckpt, NUM_CHECKPOINTS, mins / 60, mins % 60);
        printf("Altitude %5.0f m  Biome %-10s \n", g->y, BIOME_NAME[g->biome]);
        printf("Weather %-9s (changes in %2d min)  \n", WEATHER_NAME[g->wx], (int)(game_time_to_next_weather(g) / 60) + 1);
        printf("Next flag: %5.0f m %-9s        \n\n", dist, dir);
        printf("Stamina %s %3.0f \n", b1, g->stamina < 0 ? 0 : g->stamina);
        printf("Health  %s %3.0f \n", b2, g->health);
        printf("Food    %s %3.0f \n", b3, g->hunger);
        printf("Chill   %s %3.0f %s\n", b4, g->exposure, g->exposure > 30 ? "DANGER" : "      ");
        printf("Hands   L:%-5s R:%-5s  pump %2.0f/%2.0f \n", g->gripL ? "GRIP" : (g->locked[0] ? "SLIP" : "free"),
               g->gripR ? "GRIP" : (g->locked[1] ? "SLIP" : "free"), g->pump[0], g->pump[1]);
        printf("Temp %3.0fC  wearing %-13s \n", g->ambient, CLOTHING_NAME[g->clothing]);
        printf("Safe range %3.0f..%3.0fC            \n\n", g->tolMin, g->tolMax);
        if (g->rockOn && g->rockWarn > 0) printf("!! ROCKFALL in %2.0fs - MOVE SIDEWAYS !!\n", ceilf(g->rockWarn));
        else printf("                                  \n");
        printf("Surface: %-26s\n", g->state == PS_CLIMB ? (g->hooked ? "HOOKED - hands off = rest" : g->gripQ < 0.30f ? "SLICK - hands tire fast" : "good holds") : (g->state == PS_FALL ? "FALLING" : "on foot"));
        printf("Pack %2d/%2d  Grapple %d(%d)  Screws %d\n", pack_used(g), pack_capacity(g), g->inv[ITEM_GRAPPLE], g->grappleUses, g->inv[ITEM_ICE_SCREW]);
        printf("Hooks %d  Pick %d                   \n\n", g->inv[ITEM_HOOK], g->inv[ITEM_ICE_PICK]);
        printf("Y menu L/R hands A jump/loot B screw\nSELECT camera: 3rd/1st person\n");
    } else {
        printf("-- PACK %2d/%2d --  cloth %d metal %d      \n\n", pack_used(g), pack_capacity(g), g->inv[ITEM_CLOTH], g->inv[ITEM_METAL]);
        printf("CRAFT (A):\n");
        for (int i = 0; i < NUM_RECIPES; i++) {
            if (i == NUM_RECIPES - 1) {
                int c = 0, m = 0; bool ok = game_upgrade_cost(g, &c, &m);
                printf("%c Pack upgrade  %s cloth %2d metal %d   \n", sel == i ? '>' : ' ', ok ? "" : "MAX", c, m);
            } else printf("%c %-13s cloth %d metal %d  (have %d)  \n", sel == i ? '>' : ' ', RECIPES[i].name, RECIPES[i].cloth, RECIPES[i].metal, g->inv[RECIPES[i].out]);
        }
        printf("\nUSE (X eat, B bandage, R wear best)    \n");
        printf("Food %d  Bandage %d  Windbr %d Jacket %d Parka %d\n", g->inv[ITEM_FOOD], g->inv[ITEM_BANDAGE], g->inv[ITEM_WINDBREAKER], g->inv[ITEM_JACKET], g->inv[ITEM_PARKA]);
        printf("\n%-40s\n", g->msgT > 0 ? g->msg : "");
        printf("\nUp/Down select  A craft  Y close\n");
    }
}

int main(void) {
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    consoleInit(GFX_BOTTOM, NULL);
    render_init();

    uint32_t seed = (uint32_t)time(NULL) % 1000000;
    enum { TITLE, CUSTOM, PLAY } mode = TITLE;
    int csel = 0, cpreset = 0; float spin = 3.14159f;
    if (!skin_load(&mySkin, SKIN_PATH)) skin_default(&mySkin);
    bool menu = false; int sel = 0, hudTick = 0;
    touchPosition lastTouch = {0}; bool touching = false;
    u64 last = svcGetSystemTick();
    draw_title(seed);

    while (aptMainLoop()) {
        hidScanInput();
        u32 down = hidKeysDown(), held = hidKeysHeld();
        u64 now = svcGetSystemTick(); float dt = (float)(now - last) / (float)SYSCLOCK_ARM11; last = now;
        if (dt > 0.05f) dt = 0.05f;

        if (mode == TITLE) {
            if (down & KEY_START) break;
            bool ch = false;
            if (down & KEY_UP) { seed++; ch = true; }
            if (down & KEY_DOWN) { seed--; ch = true; }
            if (down & KEY_RIGHT) { seed += 1000; ch = true; }
            if (down & KEY_LEFT) { seed -= 1000; ch = true; }
            if (down & KEY_X) { seed = (uint32_t)svcGetSystemTick() % 1000000; ch = true; }
            if (ch) draw_title(seed);
            if (down & KEY_A) { printf("\x1b[2J  Generating world...\n"); game_init(&game, seed); game.skin = mySkin; mode = PLAY; menu = false; printf("\x1b[2J"); }
            if (down & KEY_Y) { if (game_load(&game, SAVE_PATH)) { game.skin = mySkin; mode = PLAY; printf("\x1b[2J"); } else printf("\x1b[12;1H  No save found.   "); }
            if (down & KEY_SELECT) { mode = CUSTOM; printf("\x1b[2J"); draw_custom(&mySkin, csel, cpreset); }
            spin += dt * 0.6f;
            render_customizer(&mySkin, spin);
            continue;
        }

        if (mode == CUSTOM) {
            bool ch = false;
            if (down & KEY_UP) { csel = (csel + NUM_ROWS - 1) % NUM_ROWS; ch = true; }
            if (down & KEY_DOWN) { csel = (csel + 1) % NUM_ROWS; ch = true; }
            if (csel >= ROW_PIECE0 && csel < ROW_PRESET) {
                int p = csel - ROW_PIECE0, n = PIECE_COUNT[p];
                if (down & KEY_RIGHT) { mySkin.piece[p] = (mySkin.piece[p] + 1) % n; ch = true; }
                if (down & KEY_LEFT) { mySkin.piece[p] = (mySkin.piece[p] + n - 1) % n; ch = true; }
            } else if (csel == ROW_PRESET) {
                if (down & (KEY_RIGHT | KEY_LEFT)) { cpreset = (cpreset + ((down & KEY_RIGHT) ? 1 : SKIN_PRESETS - 1)) % SKIN_PRESETS; skin_preset(&mySkin, cpreset); ch = true; }
            } else {
                if (down & KEY_A) { edit_hex(&mySkin, csel); ch = true; }
                if (down & KEY_X) { for (int c = 0; c < 3; c++) mySkin.rgb[csel][c] = (uint8_t)(svcGetSystemTick() >> (c * 3 + 2)); ch = true; }
                if ((held & KEY_TOUCH)) {                       // touch the R/G/B bars to set a channel directly
                    touchPosition t; hidTouchRead(&t);
                    int row = t.py / 8 + 1;                      // console rows are 8px tall, 1-based
                    if (row >= SLIDER_ROW0 && row < SLIDER_ROW0 + 3) {
                        float v = ((t.px / 8.0f) - BAR_COL0) / BAR_W * 255.0f;
                        mySkin.rgb[csel][row - SLIDER_ROW0] = (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v)); ch = true;
                    } else if (row >= 3 && row < 3 + NUM_ROWS && (down & KEY_TOUCH)) { csel = row - 3; ch = true; }
                }
            }
            if (held & KEY_L) spin += dt * 2.5f;
            if (held & KEY_R) spin -= dt * 2.5f;
            if (down & KEY_B) { skin_save(&mySkin, SKIN_PATH); mode = TITLE; draw_title(seed); }
            else if (ch) draw_custom(&mySkin, csel, cpreset);
            render_customizer(&mySkin, spin);
            continue;
        }

        if (down & KEY_START) { game_save(&game, SAVE_PATH); break; }
        if (down & KEY_Y) { menu = !menu; printf("\x1b[2J"); }
        if (down & KEY_SELECT) render_toggle_view();

        if (menu) {
            if (down & KEY_UP) sel = (sel + NUM_RECIPES - 1) % NUM_RECIPES;
            if (down & KEY_DOWN) sel = (sel + 1) % NUM_RECIPES;
            if (down & KEY_A) game_craft(&game, sel);
            if (down & KEY_X) game_use(&game, ITEM_FOOD);
            if (down & KEY_B) game_use(&game, ITEM_BANDAGE);
            if (down & KEY_R) {
                if (game.inv[ITEM_PARKA]) game_use(&game, ITEM_PARKA);
                else if (game.inv[ITEM_JACKET]) game_use(&game, ITEM_JACKET);
                else game_use(&game, ITEM_WINDBREAKER);
            }
            if (game.msgT > 0) game.msgT -= dt;
        } else {
            Input in; memset(&in, 0, sizeof in);
            circlePosition cp; hidCircleRead(&cp);
            in.lx = fabsf(cp.dx) > 15 ? cp.dx / 156.0f : 0; in.ly = fabsf(cp.dy) > 15 ? cp.dy / 156.0f : 0;
            float turn = 1.8f * dt;
            if (held & KEY_DLEFT) in.lookx += turn;
            if (held & KEY_DRIGHT) in.lookx -= turn;
            if (held & KEY_DUP) in.looky += turn;
            if (held & KEY_DDOWN) in.looky -= turn;
            if (held & KEY_TOUCH) {
                touchPosition t; hidTouchRead(&t);
                if (touching) { in.lookx -= (t.px - lastTouch.px) * 0.008f; in.looky -= (t.py - lastTouch.py) * 0.008f; }
                lastTouch = t; touching = true;
            } else touching = false;
            if (held & KEY_L) in.held |= BTN_GRIP_L;
            if (held & KEY_R) in.held |= BTN_GRIP_R;
            if (down & KEY_A) in.pressed |= P_JUMP;
            if (down & KEY_B) in.pressed |= P_ANCHOR;
            if (down & KEY_X) in.pressed |= P_GRAPPLE;
            int before = game.ckpt;
            game_update(&game, dt, &in);
            if (game.ckpt != before) game_save(&game, SAVE_PATH);
        }

        if ((hudTick++ & 7) == 0) draw_hud(&game, menu, sel);
        render_frame(&game, dt);

        if (game.won) {
            printf("\x1b[2J\x1b[8;1H   *** SUMMIT REACHED ***\n   Time %d min\n\n   Press START to quit.", (int)(game.playtime / 60));
            while (aptMainLoop()) { hidScanInput(); if (hidKeysDown() & KEY_START) break; game.yaw += 0.35f * 0.016f; render_frame(&game, 0.016f); }   // slow victory orbit around the flag
            break;
        }
    }

    render_exit();
    C3D_Fini();
    gfxExit();
    return 0;
}
