/*
 * online.c — Online battles: lobby screen and message handling.
 * See online.h for how the two copies of the game stay in step.
 *
 * Payloads relayed between the players ("RELAY|..." / "MSG|..."):
 *   host  -> guest   SETUP|side|name|look|epithet   host's side and warrior
 *   guest -> host    HELLO|name|look|epithet        guest's warrior
 *   host  -> guest   START|seed|K|capture           blow the conch
 *   guest -> host    IN|dir                         a direction key or tap
 *   host  -> guest   MV|side|node                   an accepted move
 *   either           BYE                            leaving
 */
#include "online.h"

#include "net.h"
#include "portrait.h"
#include "sfx.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char CODE_LETTERS[] = "ABCDEFGHJKLMNPQRSTUVWXYZ"; /* as the server */
static bool greeted; /* HOST/JOIN sent on the current connection */

/* ------------------------------------------------------------------------ */
/* Helpers                                                                  */
/* ------------------------------------------------------------------------ */

static void relay(const char *fmt, ...)
{
    char body[200], line[220];
    va_list args;
    va_start(args, fmt);
    vsnprintf(body, sizeof body, fmt, args);
    va_end(args);
    snprintf(line, sizeof line, "RELAY|%s", body);
    net_send(line);
}

static void set_phase(App *a, OnlinePhase p)
{
    a->online_phase = p;
    a->online_time  = 0.0f;
}

static void show_info(App *a, const char *msg)
{
    printf("NET: %s%c", msg, 10);
    snprintf(a->online_msg, sizeof a->online_msg, "%.95s", msg);
    net_close();
    app_set_screen(a, SCREEN_ONLINE);
    set_phase(a, ONLINE_INFO);
}

static int connect_tries; /* attempts for the current connection */

static void connect_now(App *a)
{
    net_connect();
    greeted = false;
    connect_tries = 1;
    a->ping_timer = 20.0f;
}

static Side other(Side s) { return s == SIDE_PANDAVA ? SIDE_KAURAVA : SIDE_PANDAVA; }

/* Look fields travel as nine digits. */
static void encode_look(const Look *l, char *out)
{
    for (int i = 0; i < LOOK_FIELD_COUNT; i++)
        out[i] = (char)('0' + l->v[i]);
    out[LOOK_FIELD_COUNT] = '\0';
}

static void decode_character(Character *c, Side side, const char *name, const char *look,
                             const char *epithet)
{
    memset(c, 0, sizeof *c);
    snprintf(c->name, sizeof c->name, "%s", name && name[0] ? name : "Stranger");
    snprintf(c->epithet, sizeof c->epithet, "%s", epithet ? epithet : "");
    c->side = side;
    c->preset = -1;
    for (int i = 0; look && i < LOOK_FIELD_COUNT && look[i]; i++) {
        int v = look[i] - '0', n = look_option_count((LookField)i);
        c->look.v[i] = (uint8_t)(v >= 0 && v < n ? v : 0);
    }
}

static void send_character(const char *type, const Character *c, bool with_side)
{
    char look[LOOK_FIELD_COUNT + 1];
    encode_look(&c->look, look);
    if (with_side)
        relay("%s|%d|%s|%s|%s", type, (int)c->side, c->name, look, c->epithet);
    else
        relay("%s|%s|%s|%s", type, c->name, look, c->epithet);
}

/* Split "a|b|c" in place into at most `max` fields. */
static int split(char *s, char **f, int max)
{
    int n = 0;
    f[n++] = s;
    for (char *p = s; *p && n < max; p++)
        if (*p == '|') {
            *p = '\0';
            f[n++] = p + 1;
        }
    return n;
}

/* ------------------------------------------------------------------------ */
/* Messages                                                                 */
/* ------------------------------------------------------------------------ */

static void handle_payload(App *a, char *payload)
{
    char *f[8] = { 0 };
    int n = split(payload, f, 8);
    const char *type = f[0];

    if (!strcmp(type, "SETUP") && !a->is_host && n >= 3) {
        /* The host chose a side and a warrior: we take the other army. */
        Side host_side = atoi(f[1]) == SIDE_KAURAVA ? SIDE_KAURAVA : SIDE_PANDAVA;
        decode_character(app_character(a, host_side), host_side, f[2], n > 3 ? f[3] : "",
                         n > 4 ? f[4] : "");
        a->p1_side = other(host_side);
        a->pandava_human = a->kaurava_human = true;
        a->pandava_keys = host_side == SIDE_PANDAVA ? KEYS_NONE : KEYS_ANY;
        a->kaurava_keys = host_side == SIDE_KAURAVA ? KEYS_NONE : KEYS_ANY;
        a->creating = 0;
        a->forged[0] = a->forged[1] = false;
        app_set_screen(a, SCREEN_CREATE);
    } else if (!strcmp(type, "HELLO") && a->is_host && n >= 2) {
        Side guest = other(a->p1_side);
        decode_character(app_character(a, guest), guest, f[1], n > 2 ? f[2] : "",
                         n > 3 ? f[3] : "");
        a->peer_ready = true;
        sfx_play(SFX_UI_SELECT);
        app_set_screen(a, SCREEN_VERSUS);
    } else if (!strcmp(type, "START") && !a->is_host && n >= 4) {
        a->rotation_k = atoi(f[2]);
        if (a->rotation_k < MIN_ROTATION_K || a->rotation_k > MAX_ROTATION_K)
            a->rotation_k = DEFAULT_ROTATION_K;
        a->capture = atoi(f[3]) != 0;
        app_start_battle(a, (uint32_t)strtoul(f[1], NULL, 10));
    } else if (!strcmp(type, "IN") && a->is_host && n >= 2) {
        int d = atoi(f[1]);
        if (d >= DIR_CCW && d <= DIR_OUT)
            a->net_dir = d;
    } else if (!strcmp(type, "MV") && !a->is_host && n >= 3) {
        if (a->screen == SCREEN_BATTLE && a->game_live &&
            !game_force_move(&a->game, atoi(f[1]) ? 1 : 0, atoi(f[2])))
            show_info(a, "The two battlefields fell out of step. Please start a new battle.");
    } else if (!strcmp(type, "BYE")) {
        show_info(a, "Your opponent left the battlefield.");
    }
}

static void handle_line(App *a, char *line)
{
    if (strncmp(line, "MSG|MV|", 7) && strncmp(line, "MSG|IN|", 7) && strcmp(line, "PONG"))
        printf("NET: %s%c", line, 10); /* the browser console shows the conversation */
    if (!strncmp(line, "ROOM|", 5)) {
        snprintf(a->room, sizeof a->room, "%.7s", line + 5);
        snprintf(a->online_msg, sizeof a->online_msg, "Waiting for an opponent to join...");
    } else if (!strncmp(line, "JOINED|", 7)) {
        snprintf(a->room, sizeof a->room, "%.7s", line + 7);
        snprintf(a->online_msg, sizeof a->online_msg, "Joined! The host is sending the battle plan...");
    } else if (!strcmp(line, "PEER|joined") && a->is_host) {
        snprintf(a->online_msg, sizeof a->online_msg,
                 "An opponent has joined and is forging their warrior...");
        send_character("SETUP", app_character(a, a->p1_side), true);
        sfx_play(SFX_UI_SELECT);
    } else if (!strcmp(line, "PEER|left")) {
        show_info(a, "Your opponent left the battlefield.");
    } else if (!strncmp(line, "ERR|", 4)) {
        show_info(a, line + 4);
    } else if (!strncmp(line, "MSG|", 4)) {
        handle_payload(a, line + 4);
    }
}

void online_pump(App *a, float dt)
{
    if (a->mode != MODE_ONLINE)
        return;
    a->online_time += dt;
    NetState st = net_state();

    if (st == NET_OPEN && !greeted) {
        greeted = true;
        if (a->is_host)
            net_send("HOST|" GAME_VERSION);
        else {
            char line[48];
            snprintf(line, sizeof line, "JOIN|%s|%s", a->code_input, GAME_VERSION);
            net_send(line);
        }
    }
    if (st == NET_OPEN) {
        a->ping_timer -= dt; /* keeps idle connections from being dropped */
        if (a->ping_timer <= 0.0f) {
            net_send("PING");
            a->ping_timer = 20.0f;
        }
    }
    /* A free server occasionally drops a fresh connection: try again quietly. */
    if (st == NET_CLOSED && !greeted && connect_tries > 0 && connect_tries < 3) {
        net_connect();
        connect_tries++;
        return;
    }
    if (st == NET_CLOSED && a->online_phase != ONLINE_INFO && a->online_phase != ONLINE_CHOICE &&
        a->online_phase != ONLINE_CODE) {
        show_info(a, greeted ? "The connection to the game server was lost."
                             : "Couldn't reach the game server. Check your internet and try again.");
        return;
    }

    char line[256];
    while (net_poll(line, sizeof line))
        handle_line(a, line);
}

/* ------------------------------------------------------------------------ */
/* Flow                                                                     */
/* ------------------------------------------------------------------------ */

void online_after_forge(App *a)
{
    if (a->is_host) {
        a->room[0] = '\0';
        a->peer_ready = false;
        snprintf(a->online_msg, sizeof a->online_msg, "Connecting to the game server...");
        connect_now(a);
        app_set_screen(a, SCREEN_ONLINE);
        set_phase(a, ONLINE_HOSTING);
    } else {
        send_character("HELLO", app_character(a, a->p1_side), false);
        app_set_screen(a, SCREEN_VERSUS);
    }
}

void online_begin_battle(App *a, uint32_t seed)
{
    if (!a->is_host)
        return;
    if (seed == 0)
        seed = (uint32_t)time(NULL) ^ (uint32_t)(GetTime() * 1e6);
    relay("START|%u|%d|%d", (unsigned)seed, a->rotation_k, a->capture ? 1 : 0);
    a->net_dir = -1;
    app_start_battle(a, seed);
}

void online_leave(App *a)
{
    if (net_state() == NET_OPEN) {
        relay("BYE");
        net_send("LEAVE");
    }
    net_close();
    a->mode = MODE_SOLO;
    app_set_screen(a, SCREEN_TITLE);
}

void online_send_input(int dir)
{
    relay("IN|%d", dir);
}

void online_broadcast_moves(App *a, int runner_before, int chaser_before)
{
    /* Same order the host applied them: runner first, then chaser. */
    if (a->game.player.node != runner_before)
        relay("MV|0|%d", a->game.player.node);
    if (a->game.enemy.node != chaser_before)
        relay("MV|1|%d", a->game.enemy.node);
}

/* ------------------------------------------------------------------------ */
/* Lobby screen                                                             */
/* ------------------------------------------------------------------------ */

static void big_code(const char *code, float cx, float y, float t)
{
    const float box = 92, gap = 18;
    float x = cx - (4 * box + 3 * gap) / 2;
    for (int i = 0; i < 4; i++, x += box + gap) {
        Rectangle r = { x, y, box, box * 1.15f };
        DrawRectangleRounded(r, 0.2f, 8, C_BG);
        bool filled = code[i] != '\0' && i < (int)strlen(code);
        float pulse = !filled && i == (int)strlen(code) ? 0.5f + 0.5f * sinf(t * 5) : 0.0f;
        DrawRectangleRoundedLinesEx(r, 0.2f, 8, 3.0f, filled ? C_GOLD : Fade(C_GOLD, 0.35f + pulse * 0.5f));
        if (filled) {
            char s[2] = { code[i], '\0' };
            ui_title(s, x + box / 2, y + 14, 72, C_IVORY);
        }
    }
}

static void screen_choice(App *a)
{
    float cx = SCREEN_W / 2.0f;
    ui_title("ONLINE BATTLE", cx, 60, 64, C_GOLD);
    ui_text_center("Fight a friend on another phone or computer", cx, 150, 24, C_TEXT);

    static const char *items[] = { "Host a battle", "Join with a code", "Back" };
    int chosen = -1;
    for (int i = 0; i < 3; i++) {
        Rectangle r = { cx - 230, 260 + i * 92, 460, 70 };
        if (ui_button(r, items[i], a->sel == i, NULL))
            chosen = i;
    }
    if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W))   a->sel = (a->sel + 2) % 3;
    if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) a->sel = (a->sel + 1) % 3;
    if (IsKeyPressed(KEY_ENTER))
        chosen = a->sel;
    if (ui_back_pressed())
        chosen = 2;

    ui_text_center("The host picks an army and gets a four-letter code; the other player "
                   "types it in.", cx, 560, 20, C_DIM);
    ui_text_center(TextFormat("Game server: %s", net_server()[0] ? net_server() : "online"),
                   cx, SCREEN_H - 44, 15, Fade(C_DIM, 0.7f));

    if (chosen < 0)
        return;
    sfx_play(SFX_UI_SELECT);
    if (chosen == 0) {
        a->mode = MODE_ONLINE;
        a->is_host = true;
        app_set_screen(a, SCREEN_SIDES);
    } else if (chosen == 1) {
        a->mode = MODE_ONLINE;
        a->is_host = false;
        a->code_input[0] = '\0';
        a->sel = 0;
        set_phase(a, ONLINE_CODE);
    } else {
        online_leave(a);
    }
}

static void screen_code(App *a, float t)
{
    float cx = SCREEN_W / 2.0f;
    size_t len = strlen(a->code_input);
    ui_title("JOIN A BATTLE", cx, 40, 56, C_GOLD);
    ui_text_center("Enter the four-letter code from the host", cx, 118, 24, C_TEXT);
    big_code(a->code_input, cx, 170, t);

    /* Typing on a keyboard... */
    int ch;
    while ((ch = GetCharPressed()) > 0) {
        if (ch >= 'a' && ch <= 'z')
            ch -= 32;
        if (len < 4 && strchr(CODE_LETTERS, ch) && ch) {
            a->code_input[len++] = (char)ch;
            a->code_input[len] = '\0';
        }
    }
    if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && len > 0)
        a->code_input[--len] = '\0';

    /* ...or tapping the letter keys. */
    const float kw = 84, kh = 62, gap = 10;
    for (int i = 0; CODE_LETTERS[i]; i++) {
        int row = i / 8, col = i % 8;
        Rectangle r = { cx - (8 * kw + 7 * gap) / 2 + col * (kw + gap), 330 + row * (kh + gap), kw, kh };
        char s[2] = { CODE_LETTERS[i], '\0' };
        if (ui_button(r, s, false, NULL) && len < 4) {
            a->code_input[len++] = CODE_LETTERS[i];
            a->code_input[len] = '\0';
            sfx_play(SFX_UI_MOVE);
        }
    }
    float by = 330 + 3 * (kh + gap) + 20;
    if (ui_button((Rectangle){ cx - 380, by, 220, 64 }, "Delete", false, NULL) && len > 0)
        a->code_input[--len] = '\0';
    bool join = ui_button((Rectangle){ cx - 110, by, 220, 64 }, "Join!", len == 4, NULL) ||
                (IsKeyPressed(KEY_ENTER) && len == 4);
    if (ui_button((Rectangle){ cx + 160, by, 220, 64 }, "Back", false, NULL) || ui_back_pressed()) {
        set_phase(a, ONLINE_CHOICE);
        return;
    }
    if (join && strlen(a->code_input) == 4) {
        sfx_play(SFX_UI_SELECT);
        snprintf(a->online_msg, sizeof a->online_msg, "Connecting to the game server...");
        connect_now(a);
        set_phase(a, ONLINE_JOINING);
    }
}

static void screen_waiting(App *a, float t)
{
    float cx = SCREEN_W / 2.0f;
    bool hosting = a->online_phase == ONLINE_HOSTING;
    ui_title(hosting ? "YOUR BATTLE" : "JOINING", cx, 40, 56, C_GOLD);

    if (hosting && a->room[0]) {
        ui_text_center("Share this code with your opponent", cx, 120, 26, C_TEXT);
        big_code(a->room, cx, 170, t);
    } else {
        /* Spinning ring while connecting. */
        Vector2 c = { cx, 250 };
        DrawRing(c, 38, 46, t * 240, t * 240 + 270, 32, C_GOLD);
        if (!hosting)
            ui_text_center(TextFormat("Battle %s", a->code_input), cx, 120, 30, C_TEXT);
    }

    ui_text_center(a->online_msg, cx, 360, 24, C_IVORY);
    if (net_state() == NET_CONNECTING && a->online_time > 5.0f)
        ui_text_center("The game server may be waking up - this can take up to a minute.",
                       cx, 400, 19, C_DIM);

    const Character *me = app_character(a, a->p1_side);
    if (hosting && me->name[0]) {
        Rectangle card = { cx - 90, 450, 180, 130 };
        portrait_card(me, card, t, false);
        ui_text_center(TextFormat("%s leads the %s", me->name, side_plural(me->side)), cx, 590,
                       20, side_color(me->side));
    }

    if (ui_button((Rectangle){ cx - 110, SCREEN_H - 110, 220, 60 }, "Cancel", false, NULL) ||
        ui_back_pressed())
        online_leave(a);
}

static void screen_info(App *a)
{
    float cx = SCREEN_W / 2.0f;
    ui_title("ONLINE BATTLE", cx, 60, 56, C_GOLD);
    ui_text_center(a->online_msg, cx, 300, 26, C_IVORY);
    if (ui_button((Rectangle){ cx - 130, 420, 260, 64 }, "Back to the title", true, NULL) ||
        IsKeyPressed(KEY_ENTER) || ui_back_pressed())
        online_leave(a);
}

void screen_online(App *a, float t)
{
    /* The same dusk backdrop as the other menus. */
    DrawRectangleGradientV(0, 0, SCREEN_W, SCREEN_H, (Color){ 70, 38, 22, 255 }, C_BG);
    switch (a->online_phase) {
    case ONLINE_CHOICE:  screen_choice(a); break;
    case ONLINE_CODE:    screen_code(a, t); break;
    case ONLINE_HOSTING:
    case ONLINE_JOINING: screen_waiting(a, t); break;
    case ONLINE_INFO:    screen_info(a); break;
    }
}
