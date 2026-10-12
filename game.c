/*
 * game.c — Rules of Chakravyuha. Everything here operates on the graph,
 * the entities and the union-find; nothing draws or reads input directly.
 */
#include "game.h"

#include <stdarg.h>
#include <stdio.h>

bool game_console_log = true;

/* ------------------------------------------------------------------------ */
/* Event log                                                                */
/* ------------------------------------------------------------------------ */

static void game_log(Game *g, LogKind kind, const char *fmt, ...)
{
    LogLine *line = &g->log[g->log_count % LOG_LINES];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line->text, sizeof line->text, fmt, args);
    va_end(args);
    line->kind = kind;
    g->log_count++;

    if (game_console_log) {
        printf("[%6.2fs] %s\n", g->elapsed, line->text);
        fflush(stdout);
    }
}

const LogLine *game_log_line(const Game *g, int i)
{
    if (i < 0 || i >= g->log_count || i >= LOG_LINES)
        return NULL;
    return &g->log[(g->log_count - 1 - i) % LOG_LINES];
}

/* "R2.5" style label; rings are shown 1-based to the player. */
static const char *node_name(const Game *g, int id, char *buf, size_t size)
{
    const Node *n = &g->graph.nodes[id];
    if (id == g->graph.core)
        snprintf(buf, size, "CORE");
    else
        snprintf(buf, size, "R%d.%d", n->ring + 1, n->index);
    return buf;
}

/* ------------------------------------------------------------------------ */
/* Setup                                                                    */
/* ------------------------------------------------------------------------ */

static void update_route(Game *g)
{
    bfs_path(&g->graph, g->player.node, g->graph.core, &g->route);
}

static void replan_enemy(Game *g, const char *reason)
{
    enemy_replan(&g->enemy, &g->graph, g->player.node);
    g->events |= EV_REPLAN;
    game_log(g, LOGK_ENEMY, "%s A* (%s): %d hops, %d expanded", g->chaser_name,
             reason, g->enemy.path.length - 1, g->enemy.path.expanded);
}

void game_init(Game *g, uint32_t seed, int rotation_k,
               float cx, float cy, float radius, const GameSetup *setup)
{
    GameSetup defaults = { 0 };
    defaults.chaser_ai = true;
    if (!setup)
        setup = &defaults;

    g->seed   = seed;
    g->cx     = cx;
    g->cy     = cy;
    g->radius = radius;
    graph_build(&g->graph, cx, cy, radius, seed);
    uf_init(&g->territory, RING_COUNT + 1);

    snprintf(g->runner_name, sizeof g->runner_name, "%s",
             setup->runner_name ? setup->runner_name : "Pandava");
    snprintf(g->chaser_name, sizeof g->chaser_name, "%s",
             setup->chaser_name ? setup->chaser_name : "Kaurava");
    g->runner_ai = setup->runner_ai;
    g->chaser_ai = setup->chaser_ai;

    /* Runner on the outer ring; chaser on the opposite side of it. */
    int outer = g->graph.rings[0].count;
    int start = (int)(seed % (uint32_t)outer);
    entity_init(&g->player, graph_node_at(&g->graph, 0, start), PLAYER_SLIDE_TIME);
    entity_init(&g->enemy, graph_node_at(&g->graph, 0, start + outer / 2),
                ENEMY_SLIDE_TIME);

    g->state          = STATE_PLAYING;
    g->rotation_k     = rotation_k;
    g->since_rotation = 0;
    g->crossings      = 0;
    g->breaches       = 0;
    g->rotations      = 0;
    g->deepest_ring   = 0;
    g->capture_mode   = setup->capture;

    g->enemy_step_interval = g->chaser_ai ? ENEMY_STEP_START : HUMAN_CHASER_STEP_START;
    g->enemy_timer         = 0.0f;
    g->enemy_wake          = ENEMY_WAKE_DELAY;

    g->change_count  = 0;
    g->rotation_anim = 0.0f;
    g->log_count     = 0;
    g->events        = 0;
    g->elapsed       = 0.0f;

    game_log(g, LOGK_INFO, "Formation built (seed %u): %d nodes, K = %d",
             (unsigned)seed, g->graph.node_count, rotation_k);
    update_route(g);
    replan_enemy(g, "spawn");
}

void game_free(Game *g)
{
    graph_free(&g->graph);
}

void game_restart(Game *g, uint32_t seed)
{
    char runner[GAME_NAME_LEN], chaser[GAME_NAME_LEN];
    snprintf(runner, sizeof runner, "%s", g->runner_name);
    snprintf(chaser, sizeof chaser, "%s", g->chaser_name);
    GameSetup setup = { runner, chaser, g->runner_ai, g->chaser_ai, g->capture_mode };

    game_free(g);
    game_init(g, seed, g->rotation_k, g->cx, g->cy, g->radius, &setup);
}

/* ------------------------------------------------------------------------ */
/* Rotation                                                                 */
/* ------------------------------------------------------------------------ */

void game_rotate(Game *g)
{
    char a[16], b[16], c[16], d[16];

    /* Union-Find decides which gates are captured and must not move. */
    for (int r = 0; r < RING_COUNT; r++)
        g->graph.gates[r].locked = uf_same(&g->territory, r, r + 1);

    /* Mutate the adjacency lists: remove each old gate edge, add a new one. */
    g->change_count   = graph_rotate_gates(&g->graph, g->changes);
    g->since_rotation = 0;
    g->rotations++;
    g->rotation_anim  = ROTATION_ANIM_TIME;
    g->events |= EV_ROTATE;

    game_log(g, LOGK_ROTATE, "ROTATION #%d: %d gate(s) moved", g->rotations,
             g->change_count);
    for (int i = 0; i < g->change_count; i++) {
        const GateChange *ch = &g->changes[i];
        game_log(g, LOGK_ROTATE, "  gate %d: %s-%s -> %s-%s", ch->ring + 1,
                 node_name(g, ch->old_outer, a, sizeof a),
                 node_name(g, ch->old_inner, b, sizeof b),
                 node_name(g, ch->new_outer, c, sizeof c),
                 node_name(g, ch->new_inner, d, sizeof d));
    }

    /* The chaser speeds up a little with every rotation. */
    g->enemy_step_interval -= ENEMY_STEP_SPEEDUP;
    if (g->enemy_step_interval < ENEMY_STEP_MIN)
        g->enemy_step_interval = ENEMY_STEP_MIN;

    /* The graph changed, so every stored path is stale. */
    update_route(g);
    replan_enemy(g, "rotation");
}

/*
 * Called whenever the runner or the chaser moves across a gate edge.
 * Every K crossings (by anyone) the formation rotates.
 */
static void on_gate_crossed(Game *g, bool is_runner, int from, int to)
{
    char a[16], b[16];
    int from_ring = g->graph.nodes[from].ring;
    int to_ring   = g->graph.nodes[to].ring;
    bool inward   = to_ring > from_ring;

    g->crossings++;
    g->since_rotation++;

    if (is_runner) {
        game_log(g, LOGK_PLAYER, "%s %s gate %d: %s -> %s", g->runner_name,
                 inward ? "breached" : "fell back through",
                 (inward ? from_ring : to_ring) + 1,
                 node_name(g, from, a, sizeof a), node_name(g, to, b, sizeof b));
        if (inward) {
            g->breaches++;
            g->events |= EV_BREACH;
            if (to_ring > g->deepest_ring)
                g->deepest_ring = to_ring;

            /* Stretch goal: a crossed gate joins its two rings for good. */
            if (g->capture_mode && uf_union(&g->territory, from_ring, to_ring)) {
                g->graph.gates[from_ring].locked = true;
                g->events |= EV_CAPTURE;
                game_log(g, LOGK_CAPTURE, "Gate %d captured (%d territory sets)",
                         from_ring + 1, g->territory.sets);
            }
        }
    } else {
        g->events |= EV_ENEMY_GATE;
        game_log(g, LOGK_ENEMY, "%s crossed gate %d: %s -> %s", g->chaser_name,
                 (inward ? from_ring : to_ring) + 1,
                 node_name(g, from, a, sizeof a), node_name(g, to, b, sizeof b));
    }

    /* A move that ends the game (caught / core reached) never rotates. */
    bool decided = g->player.node == g->enemy.node || g->player.node == g->graph.core;
    if (g->state == STATE_PLAYING && !decided && g->since_rotation >= g->rotation_k)
        game_rotate(g);
}

/* ------------------------------------------------------------------------ */
/* Runner                                                                   */
/* ------------------------------------------------------------------------ */

static void check_end(Game *g)
{
    if (g->state != STATE_PLAYING)
        return;
    if (g->player.node == g->enemy.node) {
        g->state = STATE_LOST;
        g->events |= EV_LOSE;
        game_log(g, LOGK_LOSE, "%s cut down %s. The formation holds.", g->chaser_name,
                 g->runner_name);
    } else if (g->player.node == g->graph.core) {
        g->state = STATE_WON;
        g->events |= EV_WIN;
        game_log(g, LOGK_WIN, "%s BREACHED THE CORE in %.1fs!", g->runner_name,
                 g->elapsed);
    }
}

bool game_move_player_to(Game *g, int node)
{
    if (g->state != STATE_PLAYING || g->player.cooldown > 0.0f)
        return false;

    int from = g->player.node;
    if (!entity_move(&g->player, &g->graph, node)) {
        g->events |= EV_BLOCKED;
        return false;
    }
    g->player.cooldown = g->runner_ai ? RUNNER_AI_COOLDOWN : PLAYER_MOVE_COOLDOWN;
    g->events |= EV_STEP;

    int from_ring = g->graph.nodes[from].ring;
    int to_ring   = g->graph.nodes[node].ring;
    if (from_ring != to_ring)
        on_gate_crossed(g, true, from, node); /* may rotate + replan */
    check_end(g);

    if (from_ring != to_ring) {
        /*
         * Extra replan trigger (beyond rotation / reaching the target):
         * after a breach the chaser's route leads to the wrong ring.
         * Skipped if the crossing already rotated and replanned.
         */
        if (g->state == STATE_PLAYING && to_ring > from_ring &&
            g->enemy.target != g->player.node)
            replan_enemy(g, "runner breached");
    }

    update_route(g);
    return true;
}

bool game_try_move(Game *g, MoveDir dir)
{
    if (g->state != STATE_PLAYING || g->player.cooldown > 0.0f)
        return false;

    int to = entity_neighbor(&g->graph, g->player.node, dir);
    if (to < 0) {
        g->events |= EV_BLOCKED;
        return false;
    }
    return game_move_player_to(g, to);
}

/*
 * Runner AI. Two BFS distance maps turn "reach the core, avoid the chaser"
 * into a simple score over the current node and its neighbours:
 *   - never step onto the chaser;
 *   - strongly avoid nodes the chaser can reach in one step;
 *   - otherwise take the node closest to the core, breaking ties by
 *     distance from the chaser. Holding position costs a little extra.
 */
int game_autopilot_next(const Game *g)
{
    int to_core[MAX_NODES], to_chaser[MAX_NODES];
    bfs_distances(&g->graph, g->graph.core, to_core);
    bfs_distances(&g->graph, g->enemy.node, to_chaser);

    int here = g->player.node;
    int best = -1, best_score = 0;
    const Edge *e = NULL;

    for (int i = 0;; i++) {
        int cand;
        if (i == 0) {
            cand = here; /* option: hold position */
            e = g->graph.nodes[here].adj;
        } else {
            if (!e)
                break;
            cand = e->to;
            e = e->next;
        }
        if (cand == g->enemy.node)
            continue;

        int danger = to_chaser[cand] >= 0 && to_chaser[cand] <= 1;
        int far    = to_chaser[cand] < 0 ? 6 : (to_chaser[cand] > 6 ? 6 : to_chaser[cand]);
        int score  = to_core[cand] * 10 + danger * 1000 - far + (cand == here ? 3 : 0);
        if (to_core[cand] == 0)
            score = -100000; /* the core itself: always take it */

        if (best < 0 || score < best_score) {
            best = cand;
            best_score = score;
        }
    }
    return best == here ? -1 : best;
}

/* ------------------------------------------------------------------------ */
/* Chaser                                                                   */
/* ------------------------------------------------------------------------ */

/* Move the chaser one edge, handling gates, captures of the runner, etc. */
static void chaser_move_to(Game *g, int next)
{
    Entity *e = &g->enemy;
    int from = e->node;
    entity_move(e, &g->graph, next);
    g->events |= EV_ENEMY_STEP;

    if (g->graph.nodes[from].ring != g->graph.nodes[next].ring)
        on_gate_crossed(g, false, from, next); /* may rotate + replan */
    check_end(g);
}

static void enemy_step(Game *g)
{
    Entity *e = &g->enemy;

    /* Trigger: reached the node the current A* route was planned to. */
    if (enemy_reached_target(e) || e->path.length == 0)
        replan_enemy(g, "reached target");

    int next = enemy_next_node(e);
    /* Defensive: the route uses an edge that no longer exists. */
    if (next >= 0 && !graph_has_edge(&g->graph, e->node, next)) {
        replan_enemy(g, "edge vanished");
        next = enemy_next_node(e);
    }
    if (next < 0)
        return;

    e->path_pos++;
    chaser_move_to(g, next);

    if (g->state == STATE_PLAYING && enemy_reached_target(e) &&
        e->node != g->player.node)
        replan_enemy(g, "reached target");
}

bool game_try_move_chaser(Game *g, MoveDir dir)
{
    if (g->state != STATE_PLAYING || g->chaser_ai || g->enemy_wake > 0.0f ||
        g->enemy.cooldown > 0.0f)
        return false;

    int to = entity_neighbor(&g->graph, g->enemy.node, dir);
    if (to < 0) {
        g->events |= EV_BLOCKED;
        return false;
    }
    g->enemy.cooldown = g->enemy_step_interval;
    chaser_move_to(g, to);

    /* Keep the A* scout route fresh for the human chaser's overlay. */
    if (g->state == STATE_PLAYING)
        enemy_replan(&g->enemy, &g->graph, g->player.node);
    return true;
}

bool game_force_move(Game *g, int side, int node)
{
    if (g->state != STATE_PLAYING)
        return false;
    if (side == 0) {
        g->player.cooldown = 0.0f;
        return game_move_player_to(g, node);
    }
    if (!graph_has_edge(&g->graph, g->enemy.node, node))
        return false;
    g->enemy_wake = 0.0f;
    g->enemy.cooldown = g->enemy_step_interval;
    chaser_move_to(g, node);
    if (g->state == STATE_PLAYING)
        enemy_replan(&g->enemy, &g->graph, g->player.node);
    return true;
}

/* ------------------------------------------------------------------------ */
/* Update                                                                   */
/* ------------------------------------------------------------------------ */

void game_update(Game *g, float dt)
{
    entity_tick(&g->player, dt);
    entity_tick(&g->enemy, dt);

    if (g->rotation_anim > 0.0f) {
        g->rotation_anim -= dt;
        if (g->rotation_anim < 0.0f)
            g->rotation_anim = 0.0f;
    }

    if (g->state != STATE_PLAYING)
        return;
    g->elapsed += dt;

    /* AI runner: take the best move whenever the cooldown allows. */
    if (g->runner_ai && g->player.cooldown <= 0.0f) {
        int next = game_autopilot_next(g);
        if (next >= 0)
            game_move_player_to(g, next);
        if (g->state != STATE_PLAYING)
            return;
    }

    if (g->enemy_wake > 0.0f) {
        g->enemy_wake -= dt;
        return;
    }

    if (g->chaser_ai) {
        g->enemy_timer -= dt;
        if (g->enemy_timer <= 0.0f) {
            g->enemy_timer += g->enemy_step_interval;
            enemy_step(g);
        }
    }
}

/* ------------------------------------------------------------------------ */
/* Settings                                                                 */
/* ------------------------------------------------------------------------ */

void game_set_rotation_k(Game *g, int k)
{
    if (k < MIN_ROTATION_K)
        k = MIN_ROTATION_K;
    if (k > MAX_ROTATION_K)
        k = MAX_ROTATION_K;
    if (k != g->rotation_k) {
        g->rotation_k = k;
        game_log(g, LOGK_INFO, "Rotation threshold K = %d", k);
    }
}

void game_toggle_capture(Game *g)
{
    g->capture_mode = !g->capture_mode;
    game_log(g, LOGK_CAPTURE, "Gate capture (Union-Find) %s",
             g->capture_mode ? "ON" : "OFF");
}

void game_set_runner_ai(Game *g, bool on)
{
    g->runner_ai = on;
}

int game_rotation_countdown(const Game *g)
{
    int left = g->rotation_k - g->since_rotation;
    return left > 0 ? left : 0;
}
