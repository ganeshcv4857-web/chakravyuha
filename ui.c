/*
 * ui.c — Fonts, scaling, text and widgets shared by all screens.
 */
#include "ui.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>

static Font body_font, title_font;
static bool have_body, have_title;

#if defined(__ANDROID__)
bool ui_touch = true;
#else
bool ui_touch = false;
#endif

/* Try a few classical serif faces that ship with Windows. */
static bool load_font(Font *out, const char *const *candidates, int size)
{
    for (int i = 0; candidates[i]; i++) {
        if (!FileExists(candidates[i]))
            continue;
        Font f = LoadFontEx(candidates[i], size, NULL, 0);
        if (f.texture.id == 0 || f.glyphCount == 0)
            continue;
        /* Mipmaps + trilinear keep the text smooth when drawn smaller. */
        GenTextureMipmaps(&f.texture);
        SetTextureFilter(f.texture, TEXTURE_FILTER_TRILINEAR);
        *out = f;
        return true;
    }
    return false;
}

void ui_init(void)
{
    /*
     * Serif faces. The web build bundles Crimson Text (SIL Open Font License,
     * res/fonts); native builds use what the system already has.
     */
#if defined(__EMSCRIPTEN__)
    static const char *const body[]  = { "/fonts/CrimsonText-Regular.ttf", NULL };
    static const char *const title[] = { "/fonts/CrimsonText-Bold.ttf", NULL };
#elif defined(__ANDROID__)
    static const char *const body[] = {
        "/system/fonts/NotoSerif-Regular.ttf", "/system/fonts/DroidSerif-Regular.ttf",
        "/system/fonts/Roboto-Regular.ttf", NULL,
    };
    static const char *const title[] = {
        "/system/fonts/NotoSerif-Bold.ttf", "/system/fonts/DroidSerif-Bold.ttf",
        "/system/fonts/Roboto-Bold.ttf", NULL,
    };
#else
    static const char *const body[] = {
        "C:/Windows/Fonts/pala.ttf", "C:/Windows/Fonts/BOOKOS.TTF",
        "C:/Windows/Fonts/georgia.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf",
        NULL,
    };
    static const char *const title[] = {
        "C:/Windows/Fonts/palab.ttf", "C:/Windows/Fonts/BOOKOSB.TTF",
        "C:/Windows/Fonts/georgiab.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf", NULL,
    };
#endif
    have_body  = load_font(&body_font, body, 48);
    have_title = load_font(&title_font, title, 96);
    if (!have_body)
        body_font = GetFontDefault();
    if (!have_title)
        title_font = body_font;
    printf("UI: %s\n", have_body ? "serif font loaded" : "no serif font found, using raylib's");
}

void ui_close(void)
{
    if (have_body)
        UnloadFont(body_font);
    if (have_title)
        UnloadFont(title_font);
}

/* ------------------------------------------------------------------------ */
/* Canvas scaling                                                           */
/* ------------------------------------------------------------------------ */

static float    canvas_w = SCREEN_W;
static Camera2D active_cam;
static bool     cam_active;

void  ui_set_canvas_width(float w) { canvas_w = w; }
float ui_canvas_width(void) { return canvas_w; }

Camera2D ui_camera(Vector2 shake)
{
    float sw = (float)GetScreenWidth(), sh = (float)GetScreenHeight();
    float s  = fminf(sw / canvas_w, sh / SCREEN_H);
    Camera2D cam = { .zoom = s };
    cam.offset = (Vector2){ (sw - canvas_w * s) / 2 + shake.x * s,
                            (sh - SCREEN_H * s) / 2 + shake.y * s };
    return cam;
}

void ui_begin(Camera2D cam)
{
    active_cam = cam;
    cam_active = true;
    BeginMode2D(cam);
}

void ui_end(void)
{
    EndMode2D();
    cam_active = false;
}

bool     ui_camera_active(void) { return cam_active; }
Camera2D ui_current_camera(void) { return active_cam; }

Vector2 ui_mouse(void)
{
    return GetScreenToWorld2D(GetMousePosition(), ui_camera((Vector2){ 0, 0 }));
}

void ui_clip_begin(Rectangle r)
{
    Camera2D c = ui_camera((Vector2){ 0, 0 });
    BeginScissorMode((int)(c.offset.x + r.x * c.zoom), (int)(c.offset.y + r.y * c.zoom),
                     (int)(r.width * c.zoom + 0.5f), (int)(r.height * c.zoom + 0.5f));
}

void ui_clip_end(void)
{
    EndScissorMode();
}

bool ui_back_pressed(void)
{
    return IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_BACK);
}

int ui_pointers(Vector2 *out, int max)
{
    Camera2D cam = ui_camera((Vector2){ 0, 0 });
    int n = GetTouchPointCount();
    if (n > max)
        n = max;
    for (int i = 0; i < n; i++)
        out[i] = GetScreenToWorld2D(GetTouchPosition(i), cam);
    /* Desktop: the mouse acts as a single finger while its button is held. */
    if (n == 0 && max > 0 && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        out[0] = ui_mouse();
        n = 1;
    }
    return n;
}

void ui_fit_window(void)
{
#if !defined(__ANDROID__) && !defined(__EMSCRIPTEN__) /* phones and browsers fill their space */
    int     mon = GetCurrentMonitor();
    Vector2 dpi = GetWindowScaleDPI();
    float   mw  = GetMonitorWidth(mon) / dpi.x;
    float   mh  = GetMonitorHeight(mon) / dpi.y;
    float   s   = fminf(1.0f, fminf(mw * 0.94f / SCREEN_W, mh * 0.86f / SCREEN_H));
    if (s < 1.0f) {
        int w = (int)(SCREEN_W * s), h = (int)(SCREEN_H * s);
        SetWindowSize(w, h);
        SetWindowPosition((int)(mw - w) / 2, (int)(mh - h) / 2);
    }
#endif
}

/* ------------------------------------------------------------------------ */
/* Text                                                                     */
/* ------------------------------------------------------------------------ */

/* raylib's pixel font needs wider spacing than a real serif. */
static float spacing(const Font *f, float size)
{
    return (f->texture.id == GetFontDefault().texture.id) ? size / 10.0f : size * 0.02f;
}

void ui_text(const char *text, float x, float y, float size, Color c)
{
    DrawTextEx(body_font, text, (Vector2){ x, y }, size, spacing(&body_font, size), c);
}

float ui_measure(const char *text, float size)
{
    return MeasureTextEx(body_font, text, size, spacing(&body_font, size)).x;
}

void ui_text_center(const char *text, float cx, float y, float size, Color c)
{
    ui_text(text, cx - ui_measure(text, size) / 2, y, size, c);
}

void ui_text_right(const char *text, float right, float y, float size, Color c)
{
    ui_text(text, right - ui_measure(text, size), y, size, c);
}

float ui_measure_title(const char *text, float size)
{
    return MeasureTextEx(title_font, text, size, spacing(&title_font, size) * 3).x;
}

void ui_title(const char *text, float cx, float y, float size, Color c)
{
    float sp = spacing(&title_font, size) * 3; /* letter-spaced, like an inscription */
    float x  = cx - ui_measure_title(text, size) / 2;
    /* A dark offset shadow makes titles read like engraved bronze. */
    DrawTextEx(title_font, text, (Vector2){ x + 2, y + 3 }, size, sp, Fade(BLACK, 0.55f));
    DrawTextEx(title_font, text, (Vector2){ x, y }, size, sp, c);
}

/* ------------------------------------------------------------------------ */
/* Widgets                                                                  */
/* ------------------------------------------------------------------------ */

void ui_panel(Rectangle r, Color edge)
{
    DrawRectangleRounded(r, 0.06f, 8, C_PANEL);
    DrawRectangleRoundedLinesEx(r, 0.06f, 8, 2.0f, edge);
    /* Inner hairline, like a stitched leather border. */
    Rectangle in = { r.x + 5, r.y + 5, r.width - 10, r.height - 10 };
    DrawRectangleRoundedLinesEx(in, 0.06f, 8, 1.0f, Fade(edge, 0.35f));
}

bool ui_button(Rectangle r, const char *label, bool focused, bool *hovered)
{
    bool hover = CheckCollisionPointRec(ui_mouse(), r);
    if (hovered)
        *hovered = hover;
    bool hot = hover || focused;

    DrawRectangleRounded(r, 0.25f, 8, hot ? C_PANEL_HI : C_PANEL);
    DrawRectangleRoundedLinesEx(r, 0.25f, 8, hot ? 2.5f : 1.5f, hot ? C_GOLD : C_PANEL_ED);
    float size = r.height * 0.46f;
    ui_text_center(label, r.x + r.width / 2, r.y + (r.height - size) / 2 - 1, size,
                   hot ? C_GOLD : C_TEXT);
    if (focused) {
        /* Little bronze arrowheads either side of the focused button. */
        float my = r.y + r.height / 2;
        DrawTriangle((Vector2){ r.x - 16, my - 7 }, (Vector2){ r.x - 16, my + 7 },
                     (Vector2){ r.x - 6, my }, C_GOLD);
        DrawTriangle((Vector2){ r.x + r.width + 16, my + 7 },
                     (Vector2){ r.x + r.width + 16, my - 7 },
                     (Vector2){ r.x + r.width + 6, my }, C_GOLD);
    }
    return hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

int ui_arrows(Rectangle r, const char *value, bool focused)
{
    Rectangle left  = { r.x, r.y, r.height, r.height };
    Rectangle right = { r.x + r.width - r.height, r.y, r.height, r.height };
    /* Fingers are bigger than mouse pointers: widen the hit areas. */
    float grow = ui_touch ? 14.0f : 0.0f;
    Rectangle hit_l = { left.x - grow, left.y - grow, left.width + 2 * grow, left.height + 2 * grow };
    Rectangle hit_r = { right.x - grow, right.y - grow, right.width + 2 * grow, right.height + 2 * grow };
    Vector2 m = ui_mouse();
    bool hl = CheckCollisionPointRec(m, hit_l), hr = CheckCollisionPointRec(m, hit_r);
    Color base = focused ? C_GOLD : C_DIM;

    float cy = r.y + r.height / 2, s = r.height * 0.28f;
    DrawTriangle((Vector2){ left.x + left.width * 0.65f, cy - s },
                 (Vector2){ left.x + left.width * 0.35f, cy },
                 (Vector2){ left.x + left.width * 0.65f, cy + s }, hl ? C_IVORY : base);
    DrawTriangle((Vector2){ right.x + right.width * 0.35f, cy + s },
                 (Vector2){ right.x + right.width * 0.65f, cy },
                 (Vector2){ right.x + right.width * 0.35f, cy - s }, hr ? C_IVORY : base);

    float size = r.height * 0.62f;
    ui_text_center(value, r.x + r.width / 2, r.y + (r.height - size) / 2 - 1, size,
                   focused ? C_IVORY : C_TEXT);

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (hl) return -1;
        if (hr) return +1;
    }
    return 0;
}
