/*
 * app.h — Application state shared by the menu screens and the battle.
 */
#ifndef APP_H
#define APP_H

#include "character.h"
#include "game.h"
#include "ui.h"

#define GAME_VERSION "1.1.0"

/* Battlefield layout on the canvas. */
#define FIELD_CX 410.0f
#define FIELD_CY 410.0f
#define FIELD_R  360.0f
#define PANEL_X  840
#define PANEL_W  (SCREEN_W - PANEL_X - 20)

typedef enum {
    SCREEN_TITLE,
    SCREEN_HOWTO,
    SCREEN_SIDES,  /* choose Pandava / Kaurava / let the dice decide */
    SCREEN_CREATE, /* character creator, once per human player */
    SCREEN_VERSUS, /* the two warriors face off */
    SCREEN_BATTLE,
    SCREEN_ONLINE  /* online lobby: host, join with a code, wait */
} Screen;

typedef enum { MODE_SOLO, MODE_DUEL, MODE_ONLINE } Mode;

typedef enum {
    ONLINE_CHOICE,     /* host or join? */
    ONLINE_CODE,       /* typing the room code */
    ONLINE_HOSTING,    /* host: connecting, then waiting for an opponent */
    ONLINE_JOINING,    /* guest: connecting and joining the room */
    ONLINE_INFO        /* a message (opponent left, error) and a Back button */
} OnlinePhase;

/* Which keys drive a human-controlled side. */
typedef enum { KEYS_ANY, KEYS_WASD, KEYS_ARROWS, KEYS_NONE /* online opponent */ } KeySet;

typedef struct {
    int   dir;   /* buffered MoveDir, -1 = none */
    float timer;
} InputQueue;

typedef struct {
    bool  show_bfs, show_astar, show_labels, show_help;
    float shake;
    float blocked[2];       /* red flash on a blocked move: [0] runner, [1] chaser */
    float intro;            /* seconds of "battle begins" left */
    float end_timer;        /* seconds since the battle ended */
    float danger_cooldown;  /* heartbeat drum spacing */
    float glow[2];          /* HUD card pulse after a side moves */
    bool  pad_prev[2][4];   /* touch pad buttons held last frame, per side */
    InputQueue input[2];    /* [0] runner, [1] chaser */
} View;

typedef struct {
    Screen screen;
    float  screen_time;
    Mode   mode;

    /* The two warriors and who controls them. */
    Character pandava, kaurava;
    bool      pandava_human, kaurava_human;
    KeySet    pandava_keys, kaurava_keys;
    Side      p1_side;            /* solo: the human's side; duel: player 1's */

    /* Menu state. */
    int       sel;                /* focused item on the current screen */
    float     dice_timer;         /* > 0 while the dice are rolling */
    int       creating;           /* which human is in the creator (0 or 1) */
    bool      forged[2];          /* human i has finished their warrior */
    bool      keyboard;           /* on-screen keyboard open in the creator */
    Character draft;
    uint32_t  rng;

    /* Battle. */
    Game     game;
    bool     game_live;           /* game holds a graph that must be freed */
    View     view;
    uint32_t seed;
    int      rotation_k;
    bool     capture;
    bool     autopilot;           /* solo Pandava: let the BFS AI walk for you */

    /* Online battles (online.c). */
    bool        is_host;
    OnlinePhase online_phase;
    char        room[8];          /* room code, e.g. "GJHQ" */
    char        code_input[8];    /* guest: code being typed */
    char        online_msg[96];   /* status or error shown in the lobby */
    bool        peer_ready;       /* host: the guest's warrior has arrived */
    int         net_dir;          /* host: guest's latest direction, -1 = none */
    float       ping_timer;
    float       net_repeat;       /* guest: resend a held direction */
    float       online_time;      /* seconds in the current lobby phase */

    bool quit;
} App;

void app_set_screen(App *a, Screen s);
void app_start_battle(App *a, uint32_t seed);

/* How many humans need a character, and the side of human i. */
int  app_human_count(const App *a);
Side app_human_side(const App *a, int i);
Character *app_character(App *a, Side s);
const char *app_controller_label(const App *a, Side s);

/* Screens (menu.c). Each handles its own input and drawing. */
void screen_title(App *a, float t);
void screen_howto(App *a, float t);
void screen_sides(App *a, float t, float dt);
void screen_create(App *a, float t);
void screen_versus(App *a, float t);

/* Battle (render.c). */
void battle_update(App *a, float dt);
void battle_draw(App *a, float t);
void draw_battlefield_backdrop(Vector2 c, float radius, float t);

#endif /* APP_H */
