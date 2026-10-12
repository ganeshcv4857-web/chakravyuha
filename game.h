/*
 * game.h — Game rules: breaches, the rotation counter, chaser AI triggers,
 * captured gates and win/lose. Pure C, no rendering, so it can be driven by
 * main.c or by the headless tests.
 *
 * Two sides:
 *   runner  (`player`) — the Pandava breaching the formation toward the core
 *   chaser  (`enemy`)  — the Kaurava hunting the runner
 * Either side can be a human or the AI (A* chaser / BFS-guided runner).
 */
#ifndef GAME_H
#define GAME_H

#include "entity.h"
#include "graph.h"
#include "pathfind.h"
#include "unionfind.h"

/* ---- Tuning (the #ifndef ones can be overridden with -D when compiling) ---- */
#define DEFAULT_ROTATION_K   3     /* gate crossings between rotations */
#define MIN_ROTATION_K       1
#define MAX_ROTATION_K       9

#ifndef PLAYER_MOVE_COOLDOWN
#define PLAYER_MOVE_COOLDOWN 0.32f /* seconds between runner steps */
#endif
#ifndef RUNNER_AI_COOLDOWN
#define RUNNER_AI_COOLDOWN   0.38f /* the AI runner is a little slower than a human */
#endif
#define PLAYER_SLIDE_TIME    0.16f
#define ENEMY_SLIDE_TIME     0.24f
#ifndef ENEMY_STEP_START
#define ENEMY_STEP_START     0.42f /* seconds per AI chaser step at the start */
#endif
#ifndef HUMAN_CHASER_STEP_START
#define HUMAN_CHASER_STEP_START 0.34f /* a human chaser starts a little quicker */
#endif
#ifndef ENEMY_STEP_MIN
#define ENEMY_STEP_MIN       0.26f
#endif
#ifndef ENEMY_STEP_SPEEDUP
#define ENEMY_STEP_SPEEDUP   0.03f /* chaser gets this much faster per rotation */
#endif
#ifndef ENEMY_WAKE_DELAY
#define ENEMY_WAKE_DELAY     1.0f  /* grace period before the chaser moves */
#endif
#define ROTATION_ANIM_TIME   1.6f

#define LOG_LINES 12
#define LOG_TEXT  96
#define GAME_NAME_LEN 24

typedef enum { STATE_PLAYING, STATE_WON, STATE_LOST } GameState;

typedef enum {
    LOGK_INFO, LOGK_PLAYER, LOGK_ENEMY, LOGK_ROTATE, LOGK_CAPTURE, LOGK_WIN, LOGK_LOSE
} LogKind;

/* Bit flags raised during an update so the front end can play feedback. */
enum {
    EV_STEP       = 1 << 0,
    EV_BREACH     = 1 << 1, /* runner crossed a gate inward */
    EV_ROTATE     = 1 << 2,
    EV_CAPTURE    = 1 << 3,
    EV_WIN        = 1 << 4, /* runner reached the core */
    EV_LOSE       = 1 << 5, /* chaser caught the runner */
    EV_ENEMY_STEP = 1 << 6,
    EV_ENEMY_GATE = 1 << 7,
    EV_BLOCKED    = 1 << 8, /* a human pressed a direction with no edge */
    EV_REPLAN     = 1 << 9  /* chaser ran A* */
};

typedef struct {
    char    text[LOG_TEXT];
    LogKind kind;
} LogLine;

/* Who plays each side, and what they are called in the log. */
typedef struct {
    const char *runner_name;  /* NULL = "Pandava" */
    const char *chaser_name;  /* NULL = "Kaurava" */
    bool        runner_ai;    /* runner flees toward the core on its own */
    bool        chaser_ai;    /* chaser hunts with A* on its own */
    bool        capture;      /* start with gate capture on */
} GameSetup;

typedef struct {
    Graph     graph;
    UnionFind territory;      /* rings joined by captured gates */
    Entity    player, enemy;  /* runner and chaser */
    GameState state;

    uint32_t seed;
    float    cx, cy, radius;  /* layout the graph was built with */

    char runner_name[GAME_NAME_LEN];
    char chaser_name[GAME_NAME_LEN];
    bool runner_ai, chaser_ai;

    int  rotation_k;          /* rotate every K gate crossings */
    int  since_rotation;      /* crossings since the last rotation */
    int  crossings;           /* all gate crossings, runner + chaser */
    int  breaches;            /* runner's inward crossings */
    int  rotations;
    int  deepest_ring;
    bool capture_mode;        /* crossing inward captures the gate */

    Path route;               /* BFS: runner -> core, kept up to date */

    float enemy_step_interval; /* chaser seconds per step */
    float enemy_timer;
    float enemy_wake;

    GateChange changes[RING_COUNT]; /* last rotation, for the animation */
    int        change_count;
    float      rotation_anim;       /* seconds of animation left */

    LogLine  log[LOG_LINES];  /* ring buffer; log_count total lines written */
    int      log_count;
    unsigned events;          /* EV_* flags; the caller clears them */
    float    elapsed;
} Game;

/* Echo log lines to stdout (default true). */
extern bool game_console_log;

/* `setup` may be NULL: human runner vs A* chaser, default names. */
void game_init(Game *g, uint32_t seed, int rotation_k,
               float cx, float cy, float radius, const GameSetup *setup);
void game_free(Game *g);
void game_restart(Game *g, uint32_t seed); /* same sides, names and K */

/* Runner input. Both respect the move cooldown; return true if it moved. */
bool game_try_move(Game *g, MoveDir dir);
bool game_move_player_to(Game *g, int node);

/* Human chaser input (ignored while the chaser is AI or still waking). */
bool game_try_move_chaser(Game *g, MoveDir dir);

/*
 * Online play: apply a move the host has already accepted, skipping the
 * cooldown and wake-up checks (side 0 = runner, 1 = chaser). The formation
 * is fully determined by its seed and the order of moves, so replaying the
 * host's moves in order keeps both copies of the game identical.
 */
bool game_force_move(Game *g, int side, int node);

/* Advance timers, animations and any AI-controlled side by dt seconds. */
void game_update(Game *g, float dt);

/* Rotate every uncaptured gate now (also called automatically every K). */
void game_rotate(Game *g);

void game_set_rotation_k(Game *g, int k);
void game_toggle_capture(Game *g);
void game_set_runner_ai(Game *g, bool on);

/* Crossings left before the next rotation. */
int game_rotation_countdown(const Game *g);

/*
 * Runner AI: the best next node, or -1 to hold position. Uses two BFS
 * distance maps — from the core and from the chaser — to head for the core
 * while never stepping next to the chaser if it can help it.
 */
int game_autopilot_next(const Game *g);

/* i-th most recent log line (0 = newest), or NULL. */
const LogLine *game_log_line(const Game *g, int i);

#endif /* GAME_H */
