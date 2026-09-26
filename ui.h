/*
 * ui.h — Shared look and widgets for every screen: the virtual canvas,
 * battlefield palette, fonts, text, buttons and mouse handling.
 */
#ifndef UI_H
#define UI_H

#include "raylib.h"

#include <stdbool.h>

/* Everything is laid out on this canvas and scaled to the real window. */
#define SCREEN_W 1340
#define SCREEN_H 820

/* ---- Palette: the battlefield at Kurukshetra ---------------------------- */
#define C_BG        (Color){ 24, 17, 11, 255 }    /* scorched earth */
#define C_GROUND    (Color){ 96, 69, 41, 255 }    /* sunlit dust */
#define C_TRENCH    (Color){ 44, 30, 18, 255 }    /* dug earthwork */
#define C_PANEL     (Color){ 38, 27, 18, 255 }    /* leather */
#define C_PANEL_HI  (Color){ 58, 42, 27, 255 }
#define C_PANEL_ED  (Color){ 122, 90, 50, 255 }   /* bronze trim */
#define C_RING      (Color){ 178, 140, 90, 255 }  /* packed sand path */
#define C_NODE      (Color){ 232, 212, 170, 255 } /* bone */
#define C_IVORY     (Color){ 250, 238, 210, 255 }
#define C_TEXT      (Color){ 240, 226, 196, 255 } /* parchment */
#define C_DIM       (Color){ 170, 142, 102, 255 } /* faded ochre */
#define C_GATE      (Color){ 224, 170, 58, 255 }  /* burnished bronze */
#define C_GOLD      (Color){ 240, 196, 84, 255 }
#define C_CAPTURED  (Color){ 110, 220, 200, 255 } /* peacock, held by the Pandavas */
#define C_PANDAVA   (Color){ 36, 196, 178, 255 }  /* peacock teal */
#define C_KAURAVA   (Color){ 222, 52, 40, 255 }   /* blood crimson */
#define C_CORE      (Color){ 255, 128, 40, 255 }  /* sacred fire */
#define C_ROTATE    (Color){ 238, 106, 36, 255 }  /* ember */

/*
 * Touch-first controls: always on phones, or on PC with --touch (so the
 * phone layout can be tried with a mouse).
 */
extern bool ui_touch;

/* Load fonts, remember the window. Call after InitWindow. */
void ui_init(void);
void ui_close(void);

/* "Go back": Esc on a PC, the Back button on Android. */
bool ui_back_pressed(void);
void ui_new_frame(void); /* call once at the start of every frame */

/* Every finger on the screen (or the held mouse), in canvas coordinates. */
int ui_pointers(Vector2 *out, int max);

/*
 * Camera mapping the canvas onto the window, plus an optional shake. The
 * canvas is SCREEN_W wide, except the phone battle layout, which widens it
 * to match a wide phone screen (ui_set_canvas_width).
 */
Camera2D ui_camera(Vector2 shake);
void     ui_set_canvas_width(float w);
float    ui_canvas_width(void);

/*
 * BeginMode2D/EndMode2D that remember the active camera, so drawing code can
 * briefly render into a texture and then resume (see portrait.c's cache).
 */
void     ui_begin(Camera2D cam);
void     ui_end(void);
bool     ui_camera_active(void);
Camera2D ui_current_camera(void);
/* Mouse position in canvas coordinates. */
Vector2 ui_mouse(void);
/* Clip drawing to a canvas rectangle (for portraits in cards). */
void ui_clip_begin(Rectangle r);
void ui_clip_end(void);

/* Text in the battlefield serif (falls back to raylib's font). */
void  ui_text(const char *text, float x, float y, float size, Color c);
void  ui_text_center(const char *text, float cx, float y, float size, Color c);
void  ui_text_right(const char *text, float right, float y, float size, Color c);
float ui_measure(const char *text, float size);
/* Titles use the bold face. */
void  ui_title(const char *text, float cx, float y, float size, Color c);
float ui_measure_title(const char *text, float size);

/* A bronze-trimmed leather panel. */
void ui_panel(Rectangle r, Color edge);

/*
 * Clickable button. `focused` draws it highlighted (keyboard selection).
 * Returns true if clicked with the mouse this frame; *hovered is set when
 * the mouse is over it (may be NULL).
 */
bool ui_button(Rectangle r, const char *label, bool focused, bool *hovered);

/* Small "< value >" selector; returns -1 / +1 when an arrow is clicked. */
int ui_arrows(Rectangle r, const char *value, bool focused);

/* Shrink the window if the canvas does not fit on the monitor. */
void ui_fit_window(void);

#endif /* UI_H */
