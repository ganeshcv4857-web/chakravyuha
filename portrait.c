/*
 * portrait.c — Procedural warrior portraits.
 *
 * All shapes are laid out in "face units": the head is about 42 x 52 units,
 * centred near (0, 0), with y growing downward. P(x, y) maps a face-unit
 * point to the screen for the current portrait.
 */
#include "portrait.h"

#include "ui.h"

#include <math.h>
#include <string.h>

static const Color SKIN[] = {
    { 241, 196, 152, 255 }, { 224, 174, 128, 255 }, { 200, 146, 100, 255 },
    { 172, 118, 78, 255 },  { 140, 92, 60, 255 },   { 104, 68, 46, 255 },
};
static const Color HAIR[] = {
    { 30, 24, 22, 255 }, { 70, 44, 28, 255 }, { 142, 138, 132, 255 }, { 226, 224, 218, 255 },
};
static const Color ARMOUR[] = {
    { 176, 120, 58, 255 }, { 222, 176, 64, 255 }, { 178, 182, 192, 255 }, { 150, 34, 34, 255 },
    { 28, 140, 130, 255 }, { 226, 124, 34, 255 }, { 64, 58, 136, 255 },
};
#define GOLD_C   (Color){ 236, 190, 70, 255 }
#define RUBY_C   (Color){ 200, 30, 44, 255 }
#define PEARL_C  (Color){ 246, 238, 222, 255 }

/* Current portrait transform. */
static Vector2 origin;
static float   unit;

static Vector2 P(float x, float y)
{
    return (Vector2){ origin.x + x * unit, origin.y + y * unit };
}

static Color shade(Color c, float f)
{
    return (Color){ (unsigned char)(c.r * f), (unsigned char)(c.g * f),
                    (unsigned char)(c.b * f), c.a };
}

static Color mix(Color a, Color b, float t)
{
    return ColorLerp(a, b, t);
}

/* ---- Primitive helpers in face units ------------------------------------ */

/* raylib wants a fixed winding; swap two corners when needed. */
static void tri(Vector2 a, Vector2 b, Vector2 c, Color col)
{
    float cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (cross > 0)
        DrawTriangle(a, c, b, col);
    else
        DrawTriangle(a, b, c, col);
}

static void ellipse(float x, float y, float rx, float ry, Color c)
{
    DrawEllipseV(P(x, y), rx * unit, ry * unit, c);
}

static void circle(float x, float y, float r, Color c)
{
    DrawCircleV(P(x, y), r * unit, c);
}

static void line(float x0, float y0, float x1, float y1, float thick, Color c)
{
    DrawLineEx(P(x0, y0), P(x1, y1), thick * unit, c);
}

static void curve(float x0, float y0, float cx, float cy, float x1, float y1,
                  float thick, Color c)
{
    DrawSplineSegmentBezierQuadratic(P(x0, y0), P(cx, cy), P(x1, y1), thick * unit, c);
}

static void rect(float x, float y, float w, float h, Color c)
{
    DrawRectangleV(P(x, y), (Vector2){ w * unit, h * unit }, c);
}

/* Filled slice of an ellipse between two angles (degrees, screen space). */
static void ellipse_slice(float x, float y, float rx, float ry, float a0, float a1, Color c)
{
    const int steps = 28;
    Vector2 ctr = P(x, y);
    for (int i = 0; i < steps; i++) {
        float t0 = (a0 + (a1 - a0) * i / steps) * DEG2RAD;
        float t1 = (a0 + (a1 - a0) * (i + 1) / steps) * DEG2RAD;
        tri(ctr, P(x + rx * cosf(t0), y + ry * sinf(t0)),
            P(x + rx * cosf(t1), y + ry * sinf(t1)), c);
    }
}

/* ---- Features ------------------------------------------------------------ */

static void draw_hair_back(const Look *l, Color hair, bool bust)
{
    int style = l->v[LOOK_HAIR];
    if (style == HAIR_LONG) {
        if (bust)
            ellipse(0, 2, 26, 31, hair);
        else
            ellipse(0, -1, 24, 26, hair); /* token: keep inside the ring */
        if (bust) {
            /* Locks falling over the shoulders. */
            ellipse(-17, 24, 9, 16, hair);
            ellipse(17, 24, 9, 16, hair);
        }
    } else if (style == HAIR_TOPKNOT) {
        circle(0, -31, 8.5f, hair);
        circle(0, -31, 3.5f, shade(hair, 0.8f));
    }
}

static void draw_bust(const Look *l, Color skin)
{
    Color arm = ARMOUR[l->v[LOOK_ARMOUR]];
    Color dark = shade(arm, 0.62f), light = mix(arm, WHITE, 0.25f);

    /* Neck. */
    rect(-8, 12, 16, 22, shade(skin, 0.86f));
    /* Shoulders and breastplate. */
    ellipse(0, 55, 47, 25, dark);
    ellipse(0, 53, 44, 22, arm);
    /* Pauldrons. */
    circle(-33, 40, 11.5f, dark);
    circle(-33, 39, 10, light);
    circle(33, 40, 11.5f, dark);
    circle(33, 39, 10, light);
    /* Gold collar and sun emblem. */
    DrawRing(P(0, 24), 15.5f * unit, 18.5f * unit, 25, 155, 24, GOLD_C);
    for (int i = 0; i < 7; i++) {
        float a = (35 + i * 18.3f) * DEG2RAD;
        circle(cosf(a) * 20.5f, 24 + sinf(a) * 20.5f, 1.3f, RUBY_C);
    }
    circle(0, 46, 5, GOLD_C);
    circle(0, 46, 2.6f, RUBY_C);
}

static void draw_head(const Look *l, Color skin)
{
    Color ear = shade(skin, 0.9f);
    ellipse(-21, 1, 4, 6.5f, ear);
    ellipse(21, 1, 4, 6.5f, ear);
    ellipse(0, -1, 21, 26, skin);
    ellipse(0, 8, 18.5f, 17, skin); /* fuller jaw */
    /* Soft cheek shading. */
    ellipse(-13, 9, 4, 3, Fade(shade(skin, 0.75f), 0.10f));
    ellipse(13, 9, 4, 3, Fade(shade(skin, 0.75f), 0.10f));
    (void)l;
}

static void draw_earrings(const Look *l, float t)
{
    if (l->v[LOOK_EARRINGS] != 0)
        return;
    for (int s = -1; s <= 1; s += 2) {
        DrawRing(P(s * 22.0f, 10.5f), 2.2f * unit, 3.8f * unit, 0, 360, 20, GOLD_C);
        circle(s * 22.0f, 15.5f, 1.3f, PEARL_C);
        /* A glint that travels around the ring. */
        float a = t * 2.0f + s;
        circle(s * 22.0f + cosf(a) * 3.0f, 10.5f + sinf(a) * 3.0f, 0.8f, WHITE);
    }
}

static void draw_hair_front(const Look *l, Color hair, Color skin)
{
    int style = l->v[LOOK_HAIR];
    if (style == HAIR_SHAVED)
        return;
    /*
     * Hair over the crown, then the forehead redrawn in skin on top of it:
     * what remains is an arched hairline that dips into short sideburns.
     */
    ellipse(0, -14, 23, 16, hair);
    ellipse(0, -4, 17.5f, 13, skin);
    if (style == HAIR_TOPKNOT)
        DrawRing(P(0, -27), 5 * unit, 7 * unit, 180, 360, 16, GOLD_C);
}

static void draw_eyes(const Look *l, Color skin, Color hair, float t, float blink_phase)
{
    int expr = l->v[LOOK_EXPRESSION];
    float open_ry = expr == EXPR_SERENE ? 1.7f : 2.5f;
    /* Blink for ~0.12 s every 4 seconds, offset per character. */
    bool blink = fmodf(t + blink_phase, 4.0f) < 0.12f;

    for (int s = -1; s <= 1; s += 2) {
        float x = s * 8.0f;
        if (blink) {
            line(x - 4, -2, x + 4, -2, 0.9f, shade(skin, 0.55f));
        } else {
            ellipse(x, -2, 4.3f, open_ry, PEARL_C);
            circle(x, -2, 2.0f, (Color){ 64, 40, 24, 255 });
            circle(x, -2, 1.0f, BLACK);
            circle(x + 0.6f, -2.6f, 0.5f, WHITE);
        }
        /* Kohl along the upper lid. */
        curve(x - 4.5f * s, -2.3f, x, -5.0f, x + 4.5f * s, -2.3f, 0.9f,
              (Color){ 30, 20, 16, 255 });

        /* Brows set the expression. */
        Color brow = hair;
        if (expr == EXPR_FIERCE)
            line(s * 12.8f, -10.0f, s * 3.8f, -6.8f, 2.2f, brow);
        else if (expr == EXPR_SERENE)
            curve(s * 12.5f, -7.5f, s * 8.0f, -10.8f, s * 4.0f, -8.2f, 1.5f, brow);
        else
            line(s * 12.5f, -8.2f, s * 4.0f, -8.8f, 1.8f, brow);
    }
}

static void draw_nose_mouth(const Look *l, Color skin)
{
    Color sh = shade(skin, 0.72f);
    line(-1.0f, -3, -1.8f, 6, 1.0f, Fade(sh, 0.8f));
    curve(-3.6f, 7.2f, 0, 9.4f, 3.6f, 7.2f, 1.2f, sh);

    Color lip = mix(skin, (Color){ 150, 50, 52, 255 }, 0.5f);
    switch (l->v[LOOK_EXPRESSION]) {
    case EXPR_FIERCE:
        curve(-5.5f, 14.0f, 0, 11.6f, 5.5f, 14.0f, 1.7f, lip);
        break;
    case EXPR_SERENE:
        curve(-5.0f, 12.4f, 0, 15.4f, 5.0f, 12.4f, 1.6f, lip);
        break;
    default:
        line(-5, 13, 5, 13, 1.6f, lip);
        ellipse(0, 14.5f, 3.4f, 1.0f, mix(lip, skin, 0.5f));
        break;
    }
}

static void draw_beard_under(const Look *l, Color hair)
{
    int b = l->v[LOOK_FACIAL_HAIR];
    if (b == BEARD_FULL) {
        ellipse_slice(0, 7, 20.5f, 25, 0, 180, hair);
        ellipse(-18.5f, 4, 3, 7, hair); /* sideburns */
        ellipse(18.5f, 4, 3, 7, hair);
    } else if (b == BEARD_SHORT) {
        ellipse(0, 21, 9, 5.5f, hair);
        line(-19, 4, -14, 16, 2.6f, hair);
        line(19, 4, 14, 16, 2.6f, hair);
    }
}

static void draw_moustache(const Look *l, Color hair)
{
    if (l->v[LOOK_FACIAL_HAIR] == BEARD_NONE)
        return;
    for (int s = -1; s <= 1; s += 2) {
        curve(0, 10.2f, s * 4.0f, 8.6f, s * 9.0f, 11.6f, 2.4f, hair);
        curve(s * 9.0f, 11.6f, s * 11.8f, 11.6f, s * 11.2f, 8.4f, 1.5f, hair); /* curl */
    }
}

static void draw_tilak(const Look *l, float t)
{
    Color red = { 206, 32, 36, 255 }, ash = { 236, 230, 218, 255 };
    switch (l->v[LOOK_TILAK]) {
    case TILAK_VAISHNAVA:
        line(-2.3f, -14, -1.6f, -9.2f, 1.0f, ash);
        line(2.3f, -14, 1.6f, -9.2f, 1.0f, ash);
        line(0, -14, 0, -9.6f, 1.0f, red);
        break;
    case TILAK_TRIPUNDRA:
        for (int i = 0; i < 3; i++)
            line(-8, -13.6f + i * 1.6f, 8, -13.6f + i * 1.6f, 0.8f, ash);
        circle(0, -12, 1.1f, red);
        break;
    case TILAK_BINDI:
        circle(0, -11.8f, 1.7f, red);
        break;
    case TILAK_GEM: {
        float glow = 0.35f + 0.25f * sinf(t * 3.0f);
        circle(0, -11.8f, 4.5f, Fade((Color){ 120, 230, 255, 255 }, glow));
        circle(0, -11.8f, 2.3f, (Color){ 90, 210, 250, 255 });
        circle(0.7f, -12.5f, 0.7f, WHITE);
        break;
    }
    default:
        break;
    }
}

static void draw_peacock_feather(float x, float y, float t)
{
    float sway = sinf(t * 1.5f) * 1.2f;
    curve(x - 8, y + 28, x + 2 + sway, y + 14, x + sway, y, 1.2f, (Color){ 120, 150, 60, 255 });
    for (int i = 0; i < 5; i++) { /* barbs */
        float by = y + 6 + i * 4.5f;
        line(x + sway * 0.6f - 1, by, x + sway * 0.6f - 5, by + 2.5f, 0.6f,
             Fade((Color){ 110, 170, 80, 255 }, 0.8f));
        line(x + sway * 0.6f + 1, by, x + sway * 0.6f + 5, by + 2.5f, 0.6f,
             Fade((Color){ 110, 170, 80, 255 }, 0.8f));
    }
    ellipse(x + sway, y, 4.6f, 6.4f, (Color){ 32, 150, 130, 255 });
    ellipse(x + sway, y + 0.4f, 3.1f, 4.4f, (Color){ 40, 70, 170, 255 });
    ellipse(x + sway, y + 0.8f, 1.5f, 2.2f, (Color){ 160, 110, 40, 255 });
}

static void draw_headgear(const Look *l, float t)
{
    Color arm = ARMOUR[l->v[LOOK_ARMOUR]];
    switch (l->v[LOOK_HEADGEAR]) {
    case HEAD_CROWN: {
        Color g = GOLD_C, gd = shade(GOLD_C, 0.7f);
        tri(P(-19, -29), P(19, -29), P(14, -39), g);
        tri(P(-19, -29), P(14, -39), P(-14, -39), g);
        tri(P(-8, -38), P(0, -52), P(8, -38), g);     /* central spire */
        tri(P(-21, -29), P(-17, -45), P(-9, -36), g); /* side spires */
        tri(P(21, -29), P(17, -45), P(9, -36), g);
        rect(-21, -30, 42, 7, gd);
        rect(-21, -30, 42, 5.5f, g);
        for (int i = -4; i <= 4; i++)
            circle(i * 4.4f, -27.2f, 0.9f, PEARL_C);
        circle(0, -40, 3.2f, RUBY_C);
        circle(0.8f, -40.8f, 0.9f, Fade(WHITE, 0.8f));
        circle(-13, -35, 1.8f, (Color){ 40, 170, 150, 255 });
        circle(13, -35, 1.8f, (Color){ 40, 170, 150, 255 });
        circle(0, -52, 1.6f, PEARL_C);
        break;
    }
    case HEAD_TURBAN: {
        Color cloth = mix(arm, PEARL_C, 0.35f), cd = shade(cloth, 0.78f);
        ellipse(0, -20, 24.5f, 9.5f, cd);
        ellipse(0, -28, 21.5f, 12, cloth);
        curve(-21, -20, -2, -30, 16, -35, 2.4f, cd); /* wrap folds */
        curve(-18, -26, 2, -36, 14, -39, 1.6f, cd);
        ellipse(0, -18.5f, 23, 5, cloth);
        curve(1, -27, 10 + sinf(t * 1.3f), -38, 7, -50, 2.4f, PEARL_C); /* plume */
        circle(0, -24, 4.2f, GOLD_C);
        circle(0, -24, 2.2f, RUBY_C);
        break;
    }
    case HEAD_HELMET: {
        Color metal = l->v[LOOK_ARMOUR] == 1 ? GOLD_C : (Color){ 150, 150, 160, 255 };
        Color md = shade(metal, 0.66f);
        ellipse_slice(0, -12, 24.5f, 23, 180, 360, metal);
        rect(-24.5f, -14, 49, 5.5f, md);
        rect(-24.5f, -12, 5.5f, 19, metal); /* cheek guards */
        rect(19, -12, 5.5f, 19, metal);
        rect(-1.6f, -12, 3.2f, 12.5f, md); /* nose guard */
        tri(P(-3.5f, -33), P(0, -44), P(3.5f, -33), GOLD_C);
        curve(-18, -24, 0, -38, 18, -24, 1.2f, Fade(WHITE, 0.35f)); /* shine */
        break;
    }
    case HEAD_CIRCLET:
        curve(-21, -16, 0, -21, 21, -16, 2.4f, GOLD_C);
        circle(0, -18.4f, 2.3f, (Color){ 40, 170, 150, 255 });
        draw_peacock_feather(12, -46, t);
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------------ */

void portrait_draw(const Look *look, Vector2 c, float size, bool bust, float t)
{
    unit   = size / 100.0f;
    origin = (Vector2){ c.x, c.y + (bust ? -8.0f : 8.0f) * unit };

    Color skin = SKIN[look->v[LOOK_SKIN]];
    Color hair = HAIR[look->v[LOOK_HAIR_COLOR]];
    float blink_phase = look->v[LOOK_SKIN] * 0.9f + look->v[LOOK_ARMOUR] * 0.53f;

    draw_hair_back(look, hair, bust);
    if (bust)
        draw_bust(look, skin);
    draw_head(look, skin);
    draw_earrings(look, t);
    draw_hair_front(look, hair, skin);
    draw_tilak(look, t);
    draw_eyes(look, skin, hair, t, blink_phase);
    draw_beard_under(look, hair);
    draw_nose_mouth(look, skin);
    draw_moustache(look, hair);
    draw_headgear(look, t);
}

Color side_color(Side s)
{
    return s == SIDE_PANDAVA ? C_PANDAVA : C_KAURAVA;
}

/* ------------------------------------------------------------------------ */
/* Cached portraits (touch devices)                                         */
/*                                                                          */
/* Phones render each face once into a texture at the screen's real pixel  */
/* size and then draw that single image. It is far cheaper than hundreds of */
/* triangles per face every frame, and it needs no scissor clipping, which  */
/* some phones apply with the wrong offsets.                                */
/* ------------------------------------------------------------------------ */

typedef enum { KIND_CARD, KIND_TOKEN } CacheKind;

typedef struct {
    bool            used;
    CacheKind       kind;
    Look            look;
    Side            side;
    int             w, h;  /* texture size in pixels */
    RenderTexture2D rt;
    unsigned        last;  /* frame counter of last use, for eviction */
} CacheEntry;

#define CACHE_SIZE 12
static CacheEntry cache[CACHE_SIZE];
static unsigned   cache_clock;

static void card_interior(const Character *ch, Rectangle r, float t)
{
    Color edge = side_color(ch->side);
    /* Dusk sky over the battlefield, tinted toward the side's colour. */
    DrawRectangleGradientV((int)r.x, (int)r.y, (int)r.width + 1, (int)r.height + 1,
                           mix((Color){ 96, 52, 30, 255 }, edge, 0.18f), C_BG);
    DrawCircleV((Vector2){ r.x + r.width / 2, r.y + r.height * 0.42f }, r.height * 0.42f,
                Fade(mix(C_CORE, edge, 0.4f), 0.10f));
    portrait_draw(&ch->look, (Vector2){ r.x + r.width / 2, r.y + r.height * 0.56f },
                  r.height * 0.92f, true, t);
}

static void token_interior(const Character *ch, Rectangle r, float t)
{
    Vector2 c = { r.x + r.width / 2, r.y + r.height / 2 };
    float radius = r.width / 3.4f;
    DrawCircleV(c, radius, (Color){ 60, 36, 22, 255 });
    portrait_draw(&ch->look, c, radius * 2.9f, false, t);
}

/*
 * The texture holding `ch` drawn into canvas rectangle r, rendering it on a
 * miss. Rendering into a texture interrupts the current 2D camera, so it is
 * resumed afterwards.
 */
static Texture2D cached(CacheKind kind, const Character *ch, Rectangle r)
{
    float zoom = ui_camera_active() ? ui_current_camera().zoom : 1.0f;
    int w = (int)(r.width * zoom + 0.5f), h = (int)(r.height * zoom + 0.5f);
    cache_clock++;

    CacheEntry *slot = NULL;
    for (int i = 0; i < CACHE_SIZE; i++) {
        CacheEntry *e = &cache[i];
        if (e->used && e->kind == kind && e->side == ch->side && e->w == w && e->h == h &&
            memcmp(&e->look, &ch->look, sizeof e->look) == 0) {
            e->last = cache_clock;
            return e->rt.texture;
        }
        if (!slot || !e->used || (slot->used && e->last < slot->last))
            slot = e; /* free slot, or the least recently used one */
    }

    if (slot->used)
        UnloadRenderTexture(slot->rt);
    *slot = (CacheEntry){ true, kind, ch->look, ch->side, w, h, LoadRenderTexture(w, h),
                          cache_clock };
    SetTextureFilter(slot->rt.texture, TEXTURE_FILTER_BILINEAR);

    bool resume = ui_camera_active();
    if (resume)
        EndMode2D();
    BeginTextureMode(slot->rt);
    ClearBackground(BLANK);
    BeginMode2D((Camera2D){ .target = { r.x, r.y }, .zoom = zoom });
    if (kind == KIND_CARD)
        card_interior(ch, r, 0.0f);
    else
        token_interior(ch, r, 0.0f);
    EndMode2D();
    EndTextureMode();
    if (resume)
        BeginMode2D(ui_current_camera());
    return slot->rt.texture;
}

static void draw_cached(Texture2D tex, Rectangle r)
{
    /* Render textures are stored upside down: flip the source rectangle. */
    DrawTexturePro(tex, (Rectangle){ 0, 0, (float)tex.width, -(float)tex.height }, r,
                   (Vector2){ 0, 0 }, 0.0f, WHITE);
}

void portrait_card(const Character *ch, Rectangle r, float t, bool glow)
{
    Color edge = side_color(ch->side);
    float pulse = glow ? 0.5f + 0.5f * sinf(t * 5.0f) : 0.0f;

    if (ui_touch) {
        draw_cached(cached(KIND_CARD, ch, r), r);
    } else {
        ui_clip_begin(r); /* live drawing: the face blinks and jewels glint */
        card_interior(ch, r, t);
        ui_clip_end();
    }

    DrawRectangleLinesEx(r, 2.5f + 2.0f * pulse, Fade(edge, 0.75f + 0.25f * pulse));
    /* Bronze corner studs. */
    Vector2 corners[4] = { { r.x, r.y }, { r.x + r.width, r.y },
                           { r.x, r.y + r.height }, { r.x + r.width, r.y + r.height } };
    for (int i = 0; i < 4; i++) {
        DrawCircleV(corners[i], 5, C_PANEL_ED);
        DrawCircleV(corners[i], 2.5f, C_GOLD);
    }
}

void portrait_token(const Character *ch, Vector2 c, float radius, float alpha, float t)
{
    Color edge = side_color(ch->side);
    DrawCircleV(c, radius * 1.45f, Fade(edge, 0.20f * alpha));
    DrawCircleV(c, radius + 2.5f, Fade(edge, alpha));
    if (ui_touch) {
        /* The head and its headgear, which rises above the ring. */
        Rectangle r = { c.x - radius * 1.7f, c.y - radius * 1.7f, radius * 3.4f, radius * 3.4f };
        draw_cached(cached(KIND_TOKEN, ch, r), r);
    } else {
        DrawCircleV(c, radius, (Color){ 60, 36, 22, 255 });
        portrait_draw(&ch->look, c, radius * 2.9f, false, t);
    }
    if (alpha < 1.0f) /* not yet in the fight: dim it */
        DrawCircleV(c, radius, Fade(C_BG, 1.0f - alpha));
}
