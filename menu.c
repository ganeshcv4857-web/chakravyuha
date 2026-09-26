/*
 * menu.c — Title, side selection, character creator, face-off and help.
 * Immediate-mode: each screen function reads input and draws in one pass.
 */
#include "app.h"
#include "net.h"
#include "online.h"
#include "portrait.h"
#include "sfx.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------------ */
/* Shared bits                                                              */
/* ------------------------------------------------------------------------ */

static bool key_up(void)    { return IsKeyPressed(KEY_UP) || IsKeyPressedRepeat(KEY_UP); }
static bool key_down(void)  { return IsKeyPressed(KEY_DOWN) || IsKeyPressedRepeat(KEY_DOWN); }
static bool key_left(void)  { return IsKeyPressed(KEY_LEFT) || IsKeyPressedRepeat(KEY_LEFT); }
static bool key_right(void) { return IsKeyPressed(KEY_RIGHT) || IsKeyPressedRepeat(KEY_RIGHT); }
static bool key_ok(void)    { return IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER); }

/* Move a selection with wrap-around, with a tick sound. */
static void step_sel(int *sel, int delta, int count)
{
    *sel = (*sel + delta + count) % count;
    sfx_play(SFX_UI_MOVE);
}

/* Dusk sky and dust behind every menu. */
static void menu_backdrop(float t)
{
    DrawRectangleGradientV(0, 0, SCREEN_W, SCREEN_H, (Color){ 70, 38, 22, 255 }, C_BG);
    /* A low sun behind the dust. */
    DrawCircleGradient((Vector2){ SCREEN_W / 2.0f, SCREEN_H * 0.62f }, 420,
                       Fade(C_CORE, 0.22f), Fade(C_BG, 0.0f));
    /* Drifting dust motes. */
    for (int i = 0; i < 70; i++) {
        float sx = fmodf(i * 97.31f + t * (6 + i % 7), SCREEN_W + 40) - 20;
        float sy = fmodf(i * 53.17f + sinf(t * 0.3f + i) * 12, SCREEN_H);
        DrawCircleV((Vector2){ sx, sy }, 1.2f + (i % 3) * 0.6f, Fade(C_IVORY, 0.05f + (i % 4) * 0.02f));
    }
}

/* The formation as an ornament: concentric rings turning both ways. */
static void formation_emblem(Vector2 c, float radius, float t, float alpha)
{
    for (int r = 0; r < 5; r++) {
        float rad = radius * (5 - r) / 5.0f;
        int nodes = 12 - 2 * r;
        float spin = t * (r % 2 ? -0.12f : 0.09f);
        DrawRing(c, rad - 1.5f, rad + 1.5f, 0, 360, 96, Fade(C_RING, 0.45f * alpha));
        for (int i = 0; i < nodes; i++) {
            float a = spin + i * 2 * PI / nodes;
            DrawCircleV((Vector2){ c.x + cosf(a) * rad, c.y + sinf(a) * rad }, 4,
                        Fade(i == 0 ? C_GATE : C_NODE, 0.7f * alpha));
        }
    }
    DrawCircleV(c, 16 + 3 * sinf(t * 3), Fade(C_CORE, 0.5f * alpha));
    DrawPoly(c, 6, 8, t * 40, Fade(C_IVORY, alpha));
}

/* ------------------------------------------------------------------------ */
/* Title                                                                    */
/* ------------------------------------------------------------------------ */

void screen_title(App *a, float t)
{
    /* Menu for this platform: online play needs the browser's networking,
       and a web page can't close itself. */
    enum { ITEM_SOLO, ITEM_DUEL, ITEM_ONLINE, ITEM_HOWTO, ITEM_QUIT };
    const char *items[5];
    int ids[5], n = 0;
    items[n] = "Solo Battle", ids[n++] = ITEM_SOLO;
    items[n] = ui_touch ? "Two Warriors  (one device)" : "Two Warriors  (one keyboard)",
    ids[n++] = ITEM_DUEL;
    if (net_available())
        items[n] = "Online Battle", ids[n++] = ITEM_ONLINE;
    items[n] = "How to Play", ids[n++] = ITEM_HOWTO;
#if !defined(__EMSCRIPTEN__)
    items[n] = "Leave the Field", ids[n++] = ITEM_QUIT;
#endif
    if (a->sel >= n)
        a->sel = 0;

    if (key_up() || IsKeyPressed(KEY_W))   step_sel(&a->sel, -1, n);
    if (key_down() || IsKeyPressed(KEY_S)) step_sel(&a->sel, +1, n);

    menu_backdrop(t);
    formation_emblem((Vector2){ SCREEN_W / 2.0f, 470 }, 300, t, 0.35f);

    ui_title("CHAKRAVYUHA", SCREEN_W / 2.0f, 58, 88, C_GOLD);
    ui_text_center("The spiral formation of Kurukshetra", SCREEN_W / 2.0f, 160, 26, C_TEXT);
    ui_text_center("Breach five rotating rings  -  or guard them", SCREEN_W / 2.0f, 194, 19,
                   C_DIM);

    /* Legendary warriors flank the menu. */
    static Character left, right;
    if (!left.name[0]) {
        character_from_preset(&left, SIDE_PANDAVA, 0);   /* Abhimanyu */
        character_from_preset(&right, SIDE_KAURAVA, 4);  /* Jayadratha */
    }
    portrait_card(&left, (Rectangle){ 90, 270, 240, 300 }, t, false);
    ui_text_center(left.name, 210, 580, 26, C_PANDAVA);
    ui_text_center(left.epithet, 210, 612, 16, C_DIM);
    portrait_card(&right, (Rectangle){ SCREEN_W - 330, 270, 240, 300 }, t, false);
    ui_text_center(right.name, SCREEN_W - 210, 580, 26, C_KAURAVA);
    ui_text_center(right.epithet, SCREEN_W - 210, 612, 16, C_DIM);

    int chosen = -1;
    float step = n > 4 ? 66 : 74;
    for (int i = 0; i < n; i++) {
        Rectangle r = { SCREEN_W / 2.0f - 210, 290 + i * step, 420, 56 };
        bool hover;
        if (ui_button(r, items[i], a->sel == i, &hover))
            chosen = i;
        if (hover && a->sel != i && (GetMouseDelta().x != 0 || GetMouseDelta().y != 0))
            a->sel = i;
    }
    if (key_ok() || IsKeyPressed(KEY_SPACE))
        chosen = a->sel;
#if !defined(__EMSCRIPTEN__)
    if (ui_back_pressed())
        chosen = n - 1; /* leave the field */
#endif

    ui_text_center("A Data Structures & Algorithms project  -  BFS, A* and Union-Find on a "
                   "graph that rewires itself",
                   SCREEN_W / 2.0f, SCREEN_H - 40, 16, Fade(C_DIM, 0.9f));
    ui_text_right("v" GAME_VERSION, SCREEN_W - 16, SCREEN_H - 28, 14, C_DIM);
    if (sfx_pack_files() > 0)
        ui_text(TextFormat("Sound pack: %d files", sfx_pack_files()), 16, SCREEN_H - 28, 14,
                C_DIM);

    if (chosen < 0)
        return;
    sfx_play(SFX_UI_SELECT);
    switch (ids[chosen]) {
    case ITEM_SOLO: a->mode = MODE_SOLO; app_set_screen(a, SCREEN_SIDES); break;
    case ITEM_DUEL: a->mode = MODE_DUEL; app_set_screen(a, SCREEN_SIDES); break;
    case ITEM_ONLINE:
        app_set_screen(a, SCREEN_ONLINE);
        a->online_phase = ONLINE_CHOICE;
        break;
    case ITEM_HOWTO: app_set_screen(a, SCREEN_HOWTO); break;
    default: a->quit = true; break;
    }
}

/* ------------------------------------------------------------------------ */
/* How to play                                                              */
/* ------------------------------------------------------------------------ */

void screen_howto(App *a, float t)
{
    menu_backdrop(t);
    ui_title("HOW TO PLAY", SCREEN_W / 2.0f, 40, 60, C_GOLD);

    ui_panel((Rectangle){ 70, 130, 580, 560 }, C_PANDAVA);
    ui_panel((Rectangle){ 690, 130, 580, 560 }, C_KAURAVA);

    const char *left[] = {
        "THE PANDAVA  -  breach the formation",
        "",
        "Five rings of rooms surround the sacred fire.",
        "Rings connect only through one bronze GATE each.",
        "",
        "A / D  or  Left / Right   walk around your ring",
        "W  or  Up                 go through a gate inward",
        "S  or  Down               fall back outward",
        "",
        "Reach the fire at the centre to win.",
        "Letter badges beside you show your moves.",
        "",
        "B shows your shortest route (Breadth-First Search).",
    };
    if (ui_touch) { /* the same guide, for fingers instead of keys */
        left[5]  = "Tap a glowing room beside you to step there,";
        left[6]  = "or use the arrow pad: left / right walk the ring,";
        left[7]  = "up goes through a gate, down falls back outward.";
        left[10] = "Glowing rings show every step you can take.";
        left[12] = "Route shows your shortest path (Breadth-First Search).";
    }
    const char *right[] = {
        "THE KAURAVA  -  guard the formation",
        "",
        "Land on the Pandava's room to cut them down.",
        "The AI Kaurava hunts with A* search, re-planning",
        "whenever the formation shifts.",
        "",
        "THE FORMATION SHIFTS",
        "Every K gate crossings (by either side) every gate",
        "jumps to a new room: edges are removed from the",
        "graph and re-inserted, and every route is re-found.",
        "",
        "C: captured gates stay put (Union-Find).",
        "V shows the Kaurava's A* route.",
    };
    if (ui_touch) {
        right[11] = "Capture: captured gates stay put (Union-Find).";
        right[12] = "A* path shows the Kaurava's A* route.";
    }
    for (int i = 0; i < 13; i++) {
        bool head = i == 0 || i == 6;
        ui_text(left[i], 96, 156 + i * 38, i == 0 ? 22 : 18, i == 0 ? C_PANDAVA : C_TEXT);
        ui_text(right[i], 716, 156 + i * 38, head ? 22 : 18,
                i == 0 ? C_KAURAVA : (head ? C_ROTATE : C_TEXT));
    }

    bool back = ui_button((Rectangle){ SCREEN_W / 2.0f - 120, 720, 240, 54 }, "Back  (Esc)",
                          true, NULL);
    if (back || key_ok() || ui_back_pressed() || IsKeyPressed(KEY_BACKSPACE)) {
        sfx_play(SFX_UI_SELECT);
        app_set_screen(a, SCREEN_TITLE);
    }
}

/* ------------------------------------------------------------------------ */
/* Choose a side                                                            */
/* ------------------------------------------------------------------------ */

static void draw_die(Vector2 c, float size, int face, float angle, Color col)
{
    Rectangle r = { c.x, c.y, size, size };
    DrawRectanglePro(r, (Vector2){ size / 2, size / 2 }, angle, col);
    static const float pips[7][6][2] = {
        { { 0 } },
        { { 0, 0 } },
        { { -1, -1 }, { 1, 1 } },
        { { -1, -1 }, { 0, 0 }, { 1, 1 } },
        { { -1, -1 }, { 1, -1 }, { -1, 1 }, { 1, 1 } },
        { { -1, -1 }, { 1, -1 }, { 0, 0 }, { -1, 1 }, { 1, 1 } },
        { { -1, -1 }, { 1, -1 }, { -1, 0 }, { 1, 0 }, { -1, 1 }, { 1, 1 } },
    };
    float s = sinf(angle * DEG2RAD), co = cosf(angle * DEG2RAD), d = size * 0.27f;
    for (int i = 0; i < face; i++) {
        float x = pips[face][i][0] * d, y = pips[face][i][1] * d;
        DrawCircleV((Vector2){ c.x + x * co - y * s, c.y + x * s + y * co }, size * 0.08f,
                    C_BG);
    }
}

static void assign_sides(App *a, Side first)
{
    a->p1_side = first;
    if (a->mode == MODE_ONLINE) {
        /* Both warriors are people; the opponent's moves arrive over the network. */
        a->pandava_human = a->kaurava_human = true;
        a->pandava_keys = first == SIDE_PANDAVA ? KEYS_ANY : KEYS_NONE;
        a->kaurava_keys = first == SIDE_KAURAVA ? KEYS_ANY : KEYS_NONE;
    } else if (a->mode == MODE_SOLO) {
        a->pandava_human = first == SIDE_PANDAVA;
        a->kaurava_human = first == SIDE_KAURAVA;
        a->pandava_keys = a->kaurava_keys = KEYS_ANY;
    } else {
        a->pandava_human = a->kaurava_human = true;
        a->pandava_keys = first == SIDE_PANDAVA ? KEYS_WASD : KEYS_ARROWS;
        a->kaurava_keys = first == SIDE_KAURAVA ? KEYS_WASD : KEYS_ARROWS;
    }
    a->creating = 0;
    a->forged[0] = a->forged[1] = false;
    app_set_screen(a, SCREEN_CREATE);
}

void screen_sides(App *a, float t, float dt)
{
    bool duel = a->mode == MODE_DUEL, online = a->mode == MODE_ONLINE;
    bool rolling = a->dice_timer > 0;

    if (!rolling) {
        if (key_left() || IsKeyPressed(KEY_A))  step_sel(&a->sel, -1, 3);
        if (key_right() || IsKeyPressed(KEY_D)) step_sel(&a->sel, +1, 3);
        if (ui_back_pressed() || IsKeyPressed(KEY_BACKSPACE)) {
            if (online) {
                app_set_screen(a, SCREEN_ONLINE);
                a->online_phase = ONLINE_CHOICE;
            } else {
                app_set_screen(a, SCREEN_TITLE);
            }
            return;
        }
    }

    menu_backdrop(t);
    ui_title(duel ? "PLAYER 1, CHOOSE YOUR SIDE" : "CHOOSE YOUR SIDE", SCREEN_W / 2.0f, 40,
             54, C_GOLD);
    ui_text_center(online ? "Your online opponent will lead the other army"
                   : duel ? (ui_touch ? "Player 1 takes the left arrow pad  -  Player 2 the right"
                                      : "Player 1 moves with WASD  -  Player 2 with the arrow keys")
                          : "Whichever side you do not take, the AI will command",
                   SCREEN_W / 2.0f, 116, 20, C_TEXT);

    static Character hero, villain;
    if (!hero.name[0]) {
        character_from_preset(&hero, SIDE_PANDAVA, 1);   /* Arjuna */
        character_from_preset(&villain, SIDE_KAURAVA, 0); /* Duryodhana */
    }

    const char *titles[3] = { "THE PANDAVAS", "THE KAURAVAS", "SHAKUNI'S DICE" };
    const char *lines[3][3] = {
        { "Breach the formation.", "Reach the sacred fire", "at its heart." },
        { "Guard the formation.", "Cut down the warrior", "who dares to enter." },
        { "Let the dice decide", "which army you", "will lead today." },
    };
    const char *ai_line[3] = {
        online ? "Your opponent leads the Kauravas"
               : duel ? "Player 2 leads the Kauravas" : "The Kaurava AI hunts you with A*",
        online ? "Your opponent leads the Pandavas"
               : duel ? "Player 2 leads the Pandavas" : "The Pandava AI flees along BFS routes",
        "Shakuni's dice were never fair...",
    };
    Color cols[3] = { C_PANDAVA, C_KAURAVA, C_GOLD };

    /* While rolling, the highlight flickers between the two armies. */
    int shown = a->sel;
    if (rolling) {
        a->dice_timer -= dt;
        float speed = 4 + 30 * a->dice_timer;
        shown = ((int)(t * speed)) % 2;
    }

    int chosen = -1;
    for (int i = 0; i < 3; i++) {
        Rectangle r = { 95 + i * 395.0f, 160, 360, 520 };
        bool hot = shown == i;
        bool hover = CheckCollisionPointRec(ui_mouse(), r);
        if (!rolling && hover && (GetMouseDelta().x != 0 || GetMouseDelta().y != 0))
            a->sel = i;
        if (!rolling && hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            chosen = i;

        DrawRectangleRounded(r, 0.05f, 8, hot ? C_PANEL_HI : C_PANEL);
        DrawRectangleRoundedLinesEx(r, 0.05f, 8, hot ? 4.0f : 2.0f,
                                    hot ? cols[i] : Fade(cols[i], 0.45f));
        ui_title(titles[i], r.x + r.width / 2, r.y + 22, 30, cols[i]);

        Rectangle art = { r.x + 60, r.y + 76, 240, 250 };
        if (i < 2) {
            portrait_card(i == 0 ? &hero : &villain, art, t, hot);
        } else {
            DrawRectangleRec(art, Fade(C_BG, 0.6f));
            DrawRectangleLinesEx(art, 2.5f, Fade(C_GOLD, 0.8f));
            float spin = rolling ? t * 720 : sinf(t) * 8;
            int f1 = rolling ? 1 + (int)(t * 23) % 6 : 5, f2 = rolling ? 1 + (int)(t * 17) % 6 : 2;
            draw_die((Vector2){ art.x + 85, art.y + 120 }, 76, f1, spin, C_IVORY);
            draw_die((Vector2){ art.x + 160, art.y + 140 }, 76, f2, -spin * 0.8f + 20,
                     C_NODE);
        }
        for (int k = 0; k < 3; k++)
            ui_text_center(lines[i][k], r.x + r.width / 2, r.y + 346 + k * 28, 21, C_TEXT);
        ui_text_center(ai_line[i], r.x + r.width / 2, r.y + 446, 16, Fade(cols[i], 0.9f));
    }

    if (!rolling && (key_ok() || IsKeyPressed(KEY_SPACE)))
        chosen = a->sel;

    if (chosen == 0 || chosen == 1) {
        sfx_play(SFX_UI_SELECT);
        assign_sides(a, chosen == 0 ? SIDE_PANDAVA : SIDE_KAURAVA);
    } else if (chosen == 2) {
        sfx_play(SFX_DICE);
        a->dice_timer = 1.3f;
    } else if (rolling && a->dice_timer <= 0) {
        /* The dice have spoken. */
        a->rng = a->rng * 1664525u + 1013904223u + (uint32_t)(t * 1000);
        Side s = ((a->rng >> 16) & 1) ? SIDE_KAURAVA : SIDE_PANDAVA;
        a->dice_timer = 0;
        sfx_play(SFX_UI_SELECT);
        assign_sides(a, s);
        return;
    }

    ui_text_center(ui_touch ? "Tap an army to lead it"
                            : "Left / Right to choose   -   Enter to confirm   -   Esc to go back",
                   SCREEN_W / 2.0f, SCREEN_H - 58, 17, C_DIM);
}

/* ------------------------------------------------------------------------ */
/* Character creator                                                        */
/* ------------------------------------------------------------------------ */

enum { ROW_NAME, ROW_LEGEND, ROW_FIELDS, ROW_RANDOM = ROW_FIELDS + LOOK_FIELD_COUNT,
       ROW_READY, ROW_COUNT };

/* Which preset (if any) this look still matches exactly. */
static int matching_preset(const Character *c)
{
    for (int p = 0; p < preset_count(c->side); p++) {
        Character ref;
        character_from_preset(&ref, c->side, p);
        if (memcmp(&ref.look, &c->look, sizeof ref.look) == 0)
            return p;
    }
    return -1;
}

static void change_row(App *a, int row, int delta)
{
    Character *d = &a->draft;
    if (row == ROW_LEGEND) {
        int n = preset_count(d->side);
        int p = matching_preset(d);
        p = p < 0 ? (delta > 0 ? 0 : n - 1) : (p + delta + n) % n;
        character_from_preset(d, d->side, p);
    } else if (row >= ROW_FIELDS && row < ROW_RANDOM) {
        LookField f = (LookField)(row - ROW_FIELDS);
        int n = look_option_count(f);
        d->look.v[f] = (uint8_t)((d->look.v[f] + delta + n) % n);
    } else {
        return;
    }
    sfx_play(SFX_UI_MOVE);
}

/* Called when entering the creator for human number `a->creating`. */
static void begin_draft(App *a)
{
    Side s = app_human_side(a, a->creating);
    if (a->forged[a->creating])
        a->draft = *app_character(a, s); /* coming back to edit */
    else
        character_random_preset(&a->draft, s, &a->rng);
    a->sel = ROW_READY;
}

/* ---- On-screen keyboard (touch screens have no keys to type with) ---- */

static void name_append(Character *d, char c)
{
    size_t len = strlen(d->name);
    if (len >= NAME_MAX_LEN)
        return;
    d->name[len] = c;
    d->name[len + 1] = '\0';
    d->epithet[0] = '\0';
}

static void name_backspace(Character *d)
{
    if (d->name[0]) {
        d->name[strlen(d->name) - 1] = '\0';
        d->epithet[0] = '\0';
    }
}

static void draw_keyboard(App *a, Character *d)
{
    static const char *rows[3] = { "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM" };
    const float kw = 72, kh = 60, gap = 8;
    Rectangle panel = { 470, 420, 850, 310 };
    ui_panel(panel, C_GOLD);

    size_t len = strlen(d->name);
    bool upper = len == 0 || d->name[len - 1] == ' '; /* capitalise each word */
    for (int r = 0; r < 3; r++) {
        int n = (int)strlen(rows[r]);
        float extra = r == 2 ? 150 + gap : 0;
        float x = panel.x + (panel.width - (n * (kw + gap) - gap + extra)) / 2;
        float y = panel.y + 16 + r * (kh + gap);
        for (int i = 0; i < n; i++, x += kw + gap) {
            char c = upper ? rows[r][i] : (char)(rows[r][i] - 'A' + 'a');
            char label[2] = { c, '\0' };
            if (ui_button((Rectangle){ x, y, kw, kh }, label, false, NULL)) {
                name_append(d, c);
                sfx_play(SFX_UI_MOVE);
            }
        }
        if (r == 2 && ui_button((Rectangle){ x, y, 150, kh }, "Delete", false, NULL)) {
            name_backspace(d);
            sfx_play(SFX_UI_MOVE);
        }
    }
    float y = panel.y + 16 + 3 * (kh + gap);
    if (ui_button((Rectangle){ panel.x + 150, y, 360, kh }, "Space", false, NULL) && d->name[0])
        name_append(d, ' ');
    if (ui_button((Rectangle){ panel.x + 530, y, 170, kh }, "Done", true, NULL)) {
        a->keyboard = false;
        sfx_play(SFX_UI_SELECT);
    }
}

void screen_create(App *a, float t)
{
    Character *d = &a->draft;
    if (a->screen_time == 0.0f)
        begin_draft(a);

    /* ---- Input ---- */
    if (key_up())   step_sel(&a->sel, -1, ROW_COUNT);
    if (key_down() || IsKeyPressed(KEY_TAB)) step_sel(&a->sel, +1, ROW_COUNT);
    if (a->sel != ROW_NAME) {
        if (IsKeyPressed(KEY_W)) step_sel(&a->sel, -1, ROW_COUNT);
        if (IsKeyPressed(KEY_S)) step_sel(&a->sel, +1, ROW_COUNT);
        if (key_left() || IsKeyPressed(KEY_A))  change_row(a, a->sel, -1);
        if (key_right() || IsKeyPressed(KEY_D)) change_row(a, a->sel, +1);
    } else {
        /* Typing the name. */
        int ch;
        while ((ch = GetCharPressed()) > 0) {
            size_t len = strlen(d->name);
            bool ok = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                      (ch >= '0' && ch <= '9') || ch == ' ' || ch == '-' || ch == '\'';
            if (ok && len < NAME_MAX_LEN) {
                d->name[len] = (char)ch;
                d->name[len + 1] = '\0';
                d->epithet[0] = '\0';
            }
        }
        if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && d->name[0]) {
            d->name[strlen(d->name) - 1] = '\0';
            d->epithet[0] = '\0';
        }
    }
    if (a->sel != ROW_NAME)
        while (GetCharPressed() > 0) {} /* ignore typing off the name row */

    bool randomize = false, ready = false;
    if (a->keyboard) {
        a->sel = ROW_NAME;
        if (key_ok())
            a->keyboard = false;
    } else if (key_ok()) {
        if (a->sel == ROW_RANDOM)      randomize = true;
        else if (a->sel == ROW_READY)  ready = true;
        else                           step_sel(&a->sel, +1, ROW_COUNT);
    }
    if (ui_back_pressed() && a->keyboard) {
        a->keyboard = false;
    } else if (ui_back_pressed()) {
        if (a->creating > 0) { /* back to the previous player's warrior */
            a->creating--;
            begin_draft(a);
        } else {
            app_set_screen(a, SCREEN_SIDES);
        }
        return;
    }

    /* ---- Drawing ---- */
    menu_backdrop(t);
    Side side = d->side;
    Color sc = side_color(side);
    const char *who = a->mode == MODE_DUEL
                          ? TextFormat("PLAYER %d  -  FORGE YOUR WARRIOR",
                                       side == a->p1_side ? 1 : 2)
                          : "FORGE YOUR WARRIOR";
    ui_title(who, SCREEN_W / 2.0f, 26, 46, C_GOLD);
    ui_text_center(TextFormat("You lead the %s  -  %s", side_plural(side),
                              app_controller_label(a, side)),
                   SCREEN_W / 2.0f, 88, 20, sc);

    Rectangle card = { 70, 140, 360, 460 };
    portrait_card(d, card, t, true);
    ui_text_center(d->name[0] ? d->name : "(nameless)", card.x + card.width / 2, 612, 36, sc);
    ui_text_center(d->epithet[0] ? d->epithet : TextFormat("warrior of the %s", side_plural(side)),
                   card.x + card.width / 2, 656, 18, C_DIM);

    float x0 = 500, y0 = 132, rh = 44;
    for (int row = 0; row < ROW_RANDOM; row++) {
        float y = y0 + row * rh;
        bool focus = a->sel == row;
        Rectangle hit = { x0, y, 800, rh - 4 };
        if (!a->keyboard && CheckCollisionPointRec(ui_mouse(), hit) &&
            IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            a->sel = row;
            if (row == ROW_NAME && ui_touch)
                a->keyboard = true; /* tap the name to type it */
        }
        if (focus)
            DrawRectangleRounded(hit, 0.3f, 6, Fade(C_PANEL_HI, 0.9f));

        const char *label = row == ROW_NAME ? "Name"
                          : row == ROW_LEGEND ? "Legend"
                          : look_field_name((LookField)(row - ROW_FIELDS));
        ui_text(label, x0 + 18, y + 9, 21, focus ? C_GOLD : C_TEXT);

        Rectangle ctl = { x0 + 280, y + 4, 480, rh - 12 };
        if (row == ROW_NAME) {
            DrawRectangleRounded(ctl, 0.3f, 6, C_BG);
            DrawRectangleRoundedLinesEx(ctl, 0.3f, 6, 1.5f, focus ? C_GOLD : C_PANEL_ED);
            ui_text(d->name, ctl.x + 14, ctl.y + 4, 22, C_IVORY);
            if (focus && fmodf(t, 1.0f) < 0.55f) {
                float cx = ctl.x + 16 + ui_measure(d->name, 22);
                DrawLineEx((Vector2){ cx, ctl.y + 6 }, (Vector2){ cx, ctl.y + ctl.height - 6 },
                           2, C_GOLD);
            }
            if (ui_touch && !a->keyboard)
                ui_text_right("tap to type", ctl.x + ctl.width - 12, ctl.y + 8, 14, C_DIM);
            else if (focus && !ui_touch)
                ui_text_right("type a name", ctl.x + ctl.width - 12, ctl.y + 8, 14, C_DIM);
        } else {
            const char *value;
            if (row == ROW_LEGEND) {
                int p = matching_preset(d);
                Character ref;
                if (p >= 0)
                    character_from_preset(&ref, side, p);
                value = p >= 0 ? TextFormat("%s", ref.name) : "Custom";
            } else {
                LookField f = (LookField)(row - ROW_FIELDS);
                value = look_option_name(f, d->look.v[f]);
            }
            if (a->keyboard) { /* keyboard open: show the value, ignore taps */
                ui_text_center(value, ctl.x + ctl.width / 2, ctl.y + 6, ctl.height * 0.62f,
                               C_TEXT);
                continue;
            }
            int delta = ui_arrows(ctl, value, focus);
            if (delta) {
                a->sel = row;
                change_row(a, row, delta);
            }
        }
    }

    float by = y0 + ROW_RANDOM * rh + 16;
    if (a->keyboard) {
        draw_keyboard(a, d);
    } else {
        if (ui_button((Rectangle){ x0 + 20, by, 250, 54 }, "Random look",
                      a->sel == ROW_RANDOM, NULL))
            randomize = true;
        const char *ready_label =
            (a->creating + 1 < app_human_count(a)) ? "Next warrior" : "Ready!";
        if (ui_button((Rectangle){ x0 + 510, by, 250, 54 }, ready_label, a->sel == ROW_READY,
                      NULL))
            ready = true;
    }

    ui_text_center(ui_touch ? "Tap the arrows to change a feature   -   tap your name to type it"
                            : "Up / Down choose   -   Left / Right change   -   Enter confirm   -   "
                              "Esc back",
                   SCREEN_W / 2.0f, SCREEN_H - 40, 17, C_DIM);

    if (randomize) {
        character_randomize_look(d, &a->rng);
        sfx_play(SFX_DICE);
    }
    if (ready) {
        if (!d->name[0])
            snprintf(d->name, sizeof d->name, "%s", side == SIDE_PANDAVA ? "Nameless Pandava"
                                                                         : "Nameless Kaurava");
        *app_character(a, side) = *d;
        a->forged[a->creating] = true;
        sfx_play(SFX_UI_SELECT);
        if (a->mode == MODE_ONLINE) {
            online_after_forge(a); /* host: open the room; guest: send our warrior */
        } else if (++a->creating < app_human_count(a)) {
            begin_draft(a);
        } else {
            /* The AI side gets a legendary opponent. */
            if (a->mode == MODE_SOLO) {
                Side ai = a->p1_side == SIDE_PANDAVA ? SIDE_KAURAVA : SIDE_PANDAVA;
                character_random_preset(app_character(a, ai), ai, &a->rng);
            }
            app_set_screen(a, SCREEN_VERSUS);
        }
    }
}

/* ------------------------------------------------------------------------ */
/* Face-off                                                                 */
/* ------------------------------------------------------------------------ */

static void crossed_swords(Vector2 c, float s, Color col)
{
    for (int k = -1; k <= 1; k += 2) {
        Vector2 tip  = { c.x + k * s, c.y - s };
        Vector2 hilt = { c.x - k * s * 0.7f, c.y + s * 0.7f };
        DrawLineEx(hilt, tip, 5, col);
        /* Cross-guard: a short bar perpendicular to the blade. */
        Vector2 g = { hilt.x + (tip.x - hilt.x) * 0.12f, hilt.y + (tip.y - hilt.y) * 0.12f };
        DrawLineEx((Vector2){ g.x - s * 0.18f, g.y - k * s * 0.18f },
                   (Vector2){ g.x + s * 0.18f, g.y + k * s * 0.18f }, 5, C_GOLD);
        DrawCircleV(hilt, 5, C_GOLD); /* pommel */
    }
}

void screen_versus(App *a, float t)
{
    /* Online, only the host sets the rules and blows the conch. */
    bool guest = a->mode == MODE_ONLINE && !a->is_host;
    if (guest)
        a->sel = 3;
    const int n = 4; /* K, capture, begin, back */
    if (!guest && (key_up() || IsKeyPressed(KEY_W)))   step_sel(&a->sel, -1, n);
    if (!guest && (key_down() || IsKeyPressed(KEY_S))) step_sel(&a->sel, +1, n);
    if (a->sel == 0 && (key_left() || IsKeyPressed(KEY_A)) && a->rotation_k > MIN_ROTATION_K) {
        a->rotation_k--;
        sfx_play(SFX_UI_MOVE);
    }
    if (a->sel == 0 && (key_right() || IsKeyPressed(KEY_D)) && a->rotation_k < MAX_ROTATION_K) {
        a->rotation_k++;
        sfx_play(SFX_UI_MOVE);
    }
    if (a->sel == 1 && (key_left() || key_right() || IsKeyPressed(KEY_A) || IsKeyPressed(KEY_D))) {
        a->capture = !a->capture;
        sfx_play(SFX_UI_MOVE);
    }

    menu_backdrop(t);
    formation_emblem((Vector2){ SCREEN_W / 2.0f, 250 }, 130, t, 0.4f);

    /* The two warriors slide in from the sides. */
    float in = fminf(1.0f, a->screen_time / 0.6f);
    float e = 1 - (1 - in) * (1 - in) * (1 - in);
    Rectangle lc = { -300 + e * 400, 110, 320, 400 };
    Rectangle rc = { SCREEN_W + 0 - e * 420, 110, 320, 400 };
    portrait_card(&a->pandava, lc, t, false);
    portrait_card(&a->kaurava, rc, t, false);

    ui_text_center(a->pandava.name, lc.x + 160, 524, 38, C_PANDAVA);
    ui_text_center(a->pandava.epithet[0] ? a->pandava.epithet : "warrior of the Pandavas",
                   lc.x + 160, 570, 18, C_DIM);
    ui_text_center(app_controller_label(a, SIDE_PANDAVA), lc.x + 160, 598, 17, C_TEXT);
    ui_text_center(a->kaurava.name, rc.x + 160, 524, 38, C_KAURAVA);
    ui_text_center(a->kaurava.epithet[0] ? a->kaurava.epithet : "warrior of the Kauravas",
                   rc.x + 160, 570, 18, C_DIM);
    ui_text_center(app_controller_label(a, SIDE_KAURAVA), rc.x + 160, 598, 17, C_TEXT);

    crossed_swords((Vector2){ SCREEN_W / 2.0f, 150 }, 42, C_IVORY);
    ui_title("VS", SCREEN_W / 2.0f, 200, 72, C_GOLD);

    /* Battle options. */
    float cx = SCREEN_W / 2.0f;
    Rectangle krow = { cx - 230, 420, 460, 44 }, crow = { cx - 230, 470, 460, 44 };
    ui_panel((Rectangle){ cx - 244, 408, 488, 120 }, C_PANEL_ED);
    for (int i = 0; i < 2; i++) {
        Rectangle r = i == 0 ? krow : crow;
        if (a->sel == i)
            DrawRectangleRounded(r, 0.3f, 6, Fade(C_PANEL_HI, 0.95f));
        if (!guest && CheckCollisionPointRec(ui_mouse(), r) &&
            IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            a->sel = i;
        ui_text(i == 0 ? "Shift every K crossings" : "Gate capture (Union-Find)", r.x + 14,
                r.y + 11, 18, a->sel == i ? C_GOLD : C_TEXT);
        if (guest) { /* the host decides */
            ui_text_center(i == 0 ? "set by host" : "set by host", r.x + 370, r.y + 12, 16, C_DIM);
            continue;
        }
        int d = ui_arrows((Rectangle){ r.x + 290, r.y + 6, 160, 32 },
                          i == 0 ? TextFormat("K = %d", a->rotation_k) : (a->capture ? "On" : "Off"),
                          a->sel == i);
        if (d && i == 0) {
            a->rotation_k += d;
            if (a->rotation_k < MIN_ROTATION_K) a->rotation_k = MIN_ROTATION_K;
            if (a->rotation_k > MAX_ROTATION_K) a->rotation_k = MAX_ROTATION_K;
        } else if (d) {
            a->capture = !a->capture;
        }
    }

    bool begin = false;
    if (guest)
        ui_text_center(TextFormat("Waiting for %s to blow the conch...",
                                  app_character(a, a->p1_side == SIDE_PANDAVA ? SIDE_KAURAVA
                                                                              : SIDE_PANDAVA)->name),
                       cx, 580, 24, C_GOLD);
    else
        begin = ui_button((Rectangle){ cx - 170, 560, 340, 64 }, "Blow the conch!",
                          a->sel == 2, NULL);
    bool back = ui_button((Rectangle){ cx - 110, 646, 220, 48 },
                          a->mode == MODE_ONLINE ? "Leave" : (ui_touch ? "Back" : "Back  (Esc)"),
                          a->sel == 3, NULL);
    if (!guest && (key_ok() || IsKeyPressed(KEY_SPACE))) {
        if (a->sel == 3) back = true;
        else             begin = true;
    }
    if (ui_back_pressed())
        back = true;

    if (begin && a->mode == MODE_ONLINE) {
        online_begin_battle(a, 0);
    } else if (begin) {
        app_start_battle(a, 0); /* a fresh formation (or the --seed one) */
    } else if (back && a->mode == MODE_ONLINE) {
        online_leave(a);
    } else if (back) {
        sfx_play(SFX_UI_SELECT);
        a->creating = app_human_count(a) - 1;
        app_set_screen(a, SCREEN_CREATE);
    }
}
