/*
 * make_icon.c — Renders the Chakravyuha emblem (five rings around the sacred
 * fire) and writes every icon the Windows and Android builds need:
 *
 *   res/icon_<size>.png                                 packed into res/chakravyuha.ico
 *   android/res/mipmap-<dpi>/ic_launcher.png            launcher icon (Android 7)
 *   android/res/mipmap-<dpi>/ic_launcher_foreground.png adaptive icon (Android 8+)
 *
 * Run from the project root via `make icons`. Each icon is drawn at 4x and
 * scaled down, which gives smooth edges without MSAA.
 */
#include "raylib.h"

#include <math.h>

#define SUPERSAMPLE 4

static const Color BG     = { 24, 17, 11, 255 };
static const Color GROUND = { 110, 78, 44, 255 };
static const Color TRENCH = { 44, 30, 18, 255 };
static const Color RING   = { 196, 156, 100, 255 };
static const Color NODE   = { 240, 222, 182, 255 };
static const Color GATE   = { 236, 180, 60, 255 };
static const Color FIRE   = { 255, 128, 40, 255 };
static const Color IVORY  = { 255, 244, 214, 255 };
static const Color BRONZE = { 150, 110, 60, 255 };

static Vector2 polar(Vector2 c, float r, float a)
{
    return (Vector2){ c.x + cosf(a) * r, c.y + sinf(a) * r };
}

/*
 * The emblem in a circle of radius r. Small icons (16-32 px) get fewer,
 * bolder rings so they still read as "rings around a fire".
 */
static void emblem(Vector2 c, float r, bool small)
{
    DrawCircleV(c, r, BG);
    DrawCircleGradient(c, r * 0.97f, GROUND, BG);

    /* Spear-points around the rim. */
    if (!small)
        for (int i = 0; i < 36; i++) {
            float a = i * 2 * PI / 36;
            DrawLineEx(polar(c, r * 0.86f, a), polar(c, r * 0.95f, a), r * 0.022f, BRONZE);
        }

    const int rings = small ? 3 : 5;
    const int nodes[5] = { 12, 10, 8, 6, 4 };
    float line = r * (small ? 0.05f : 0.018f);
    for (int k = 0; k < rings; k++) {
        float rr = r * 0.80f * (rings - k) / rings;
        DrawRing(c, rr - line * 3.0f, rr + line * 3.0f, 0, 360, 128, Fade(TRENCH, 0.7f));
        DrawRing(c, rr - line, rr + line, 0, 360, 128, RING);
        if (!small)
            for (int i = 0; i < nodes[k]; i++) {
                float a = -PI / 2 + i * 2 * PI / nodes[k];
                DrawCircleV(polar(c, rr, a), r * 0.030f, NODE);
            }
    }

    /* Two bronze gates leading inward, as on the battlefield. */
    float g = r * (small ? 0.07f : 0.05f);
    float outer = r * 0.80f, inner = r * 0.80f * (rings - 1) / rings;
    DrawLineEx(polar(c, outer, -PI * 0.75f), polar(c, inner, -PI * 0.72f), g, GATE);
    if (!small) {
        float r3 = r * 0.80f * 3 / 5, r2 = r * 0.80f * 2 / 5;
        DrawLineEx(polar(c, r3, PI * 0.20f), polar(c, r2, PI * 0.22f), g, GATE);
    }

    /* The sacred fire. */
    DrawCircleV(c, r * 0.30f, Fade(FIRE, 0.20f));
    DrawCircleV(c, r * 0.20f, Fade(FIRE, 0.45f));
    for (int f = 0; f < 7; f++) {
        float a = f * 2 * PI / 7 - PI / 2;
        DrawLineEx(c, polar(c, r * 0.19f, a), r * 0.07f, FIRE);
    }
    DrawPoly(c, 6, r * 0.13f, 0, FIRE);
    DrawPoly(c, 6, r * 0.065f, 30, IVORY);
}

/* Render one icon of `size` px. `fill` is the emblem radius as a fraction. */
static void render(const char *path, int size, float fill)
{
    int big = size * SUPERSAMPLE;
    RenderTexture2D rt = LoadRenderTexture(big, big);
    BeginTextureMode(rt);
    ClearBackground(BLANK);
    emblem((Vector2){ big / 2.0f, big / 2.0f }, big * fill, size <= 32);
    EndTextureMode();

    Image img = LoadImageFromTexture(rt.texture);
    ImageFlipVertical(&img); /* render textures are stored upside down */
    ImageResize(&img, size, size);
    ExportImage(img, path);
    UnloadImage(img);
    UnloadRenderTexture(rt);
}

int main(void)
{
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(64, 64, "icons");

    /* Windows: the sizes Explorer and the taskbar ask for. */
    MakeDirectory("res");
    const int win[] = { 16, 24, 32, 48, 64, 128, 256 };
    for (int i = 0; i < 7; i++)
        render(TextFormat("res/icon_%d.png", win[i]), win[i], 0.49f);
    render("res/icon_512.png", 512, 0.49f); /* web app manifest */

    /* Android: legacy icon (48dp) and adaptive foreground (108dp canvas). */
    const char *dpi[] = { "mdpi", "hdpi", "xhdpi", "xxhdpi", "xxxhdpi" };
    const float scale[] = { 1.0f, 1.5f, 2.0f, 3.0f, 4.0f };
    for (int i = 0; i < 5; i++) {
        const char *dir = TextFormat("android/res/mipmap-%s", dpi[i]);
        MakeDirectory(dir);
        render(TextFormat("%s/ic_launcher.png", dir), (int)(48 * scale[i]), 0.48f);
        /* The emblem stays inside the 66dp safe circle of the 108dp layer. */
        render(TextFormat("%s/ic_launcher_foreground.png", dir), (int)(108 * scale[i]),
               0.30f);
    }

    CloseWindow();
    return 0;
}
