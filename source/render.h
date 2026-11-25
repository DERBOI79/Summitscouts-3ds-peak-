#pragma once
#include "game.h"

void render_init(void);
void render_exit(void);
void render_customizer(const Skin *sk, float angle);   // character preview scene (title + customiser)
void render_toggle_view(void);        // 3rd <-> 1st person
int  render_is_third_person(void);
// Draw the first-person view to the top screen. Pass NULL for the title backdrop.
void render_frame(const Game *g, float dt);
