/*
 * portrait.h — Draws a warrior's face from a Look using raylib shapes.
 */
#ifndef PORTRAIT_H
#define PORTRAIT_H

#include "character.h"
#include "raylib.h"

/*
 * Draw a portrait centred on `c`. `size` is roughly the height of the head
 * and shoulders. With `bust` false only the head (with headgear) is drawn,
 * which is what the board tokens use. `t` animates blinking and jewels.
 */
void portrait_draw(const Look *look, Vector2 c, float size, bool bust, float t);

/* Framed card: side-coloured border, dusk background, clipped bust. */
void portrait_card(const Character *ch, Rectangle r, float t, bool glow);

/* Round board token: face in a ring of the side's colour. */
void portrait_token(const Character *ch, Vector2 c, float radius, float alpha, float t);

Color side_color(Side s);

#endif /* PORTRAIT_H */
