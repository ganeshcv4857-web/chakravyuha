/*
 * test_all.c — Headless checks for the pathfinding, Union-Find and game rules.
 *
 *   make test
 *
 * BFS and A* are compared against a plain O(V^2) Dijkstra on every pair of
 * nodes, on the initial graph and after many gate rotations.
 */
#include "../entity.h"
#include "../game.h"
#include "../graph.h"
#include "../pathfind.h"
#include "../unionfind.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, ...)                                   \
    do {                                                   \
        if (!(cond)) {                                     \
            failures++;                                    \
            printf("  FAIL %s:%d: ", __FILE__, __LINE__);  \
            printf(__VA_ARGS__);                           \
            printf("\n");                                  \
        }                                                  \
    } while (0)

/* Reference shortest distances; unit = true counts hops instead of length. */
static float dijkstra(const Graph *g, int start, int goal, bool unit, int *expanded)
{
    float dist[MAX_NODES];
    bool  done[MAX_NODES] = { false };
    for (int i = 0; i < g->node_count; i++)
        dist[i] = INFINITY;
    dist[start] = 0.0f;
    *expanded = 0;

    for (;;) {
        int v = -1;
        for (int i = 0; i < g->node_count; i++)
            if (!done[i] && dist[i] < INFINITY && (v < 0 || dist[i] < dist[v]))
                v = i;
        if (v < 0)
            return INFINITY;
        done[v] = true;
        (*expanded)++;
        if (v == goal)
            return dist[v];
        for (const Edge *e = g->nodes[v].adj; e; e = e->next) {
            float w = unit ? 1.0f : node_distance(g, v, e->to);
            if (dist[v] + w < dist[e->to])
                dist[e->to] = dist[v] + w;
        }
    }
}

/* Path starts/ends in the right place and every step is a real edge. */
static bool path_valid(const Graph *g, const Path *p, int start, int goal)
{
    if (p->length < 1 || p->nodes[0] != start || p->nodes[p->length - 1] != goal)
        return false;
    for (int i = 0; i + 1 < p->length; i++)
        if (!graph_has_edge(g, p->nodes[i], p->nodes[i + 1]))
            return false;
    return true;
}

static float path_length(const Graph *g, const Path *p)
{
    float total = 0.0f;
    for (int i = 0; i + 1 < p->length; i++)
        total += node_distance(g, p->nodes[i], p->nodes[i + 1]);
    return total;
}

static void test_pathfinding(void)
{
    static Graph g;
    static Path bfs, astar;
    GateChange changes[RING_COUNT];
    long astar_expanded = 0, dijkstra_expanded = 0, pairs = 0;

    printf("pathfinding: BFS and A* vs Dijkstra, all pairs, 200 rotations\n");
    graph_build(&g, 400, 400, 350, 7);

    for (int round = 0; round <= 200; round++) {
        for (int s = 0; s < g.node_count; s++) {
            for (int t = 0; t < g.node_count; t++) {
                int ex;
                float hops = dijkstra(&g, s, t, true, &ex);
                float dist = dijkstra(&g, s, t, false, &ex);

                CHECK(bfs_path(&g, s, t, &bfs), "BFS found no path %d->%d", s, t);
                CHECK(path_valid(&g, &bfs, s, t), "BFS path %d->%d invalid", s, t);
                CHECK(bfs.length - 1 == (int)hops, "BFS %d->%d: %d hops, want %.0f",
                      s, t, bfs.length - 1, hops);

                CHECK(astar_path(&g, s, t, &astar), "A* found no path %d->%d", s, t);
                CHECK(path_valid(&g, &astar, s, t), "A* path %d->%d invalid", s, t);
                CHECK(fabsf(astar.cost - dist) < 0.01f, "A* %d->%d cost %.2f, want %.2f",
                      s, t, astar.cost, dist);
                CHECK(fabsf(path_length(&g, &astar) - astar.cost) < 0.01f,
                      "A* %d->%d reported cost does not match its path", s, t);

                astar_expanded    += astar.expanded;
                dijkstra_expanded += ex;
                pairs++;
            }
        }
        graph_rotate_gates(&g, changes);
    }
    printf("  %ld pairs checked; nodes expanded: A* %.1f avg vs Dijkstra %.1f avg\n",
           pairs, (double)astar_expanded / pairs, (double)dijkstra_expanded / pairs);

    /* A rotation must change what pathfinding returns, not just the drawing. */
    int start = graph_node_at(&g, 0, 0);
    bfs_path(&g, start, g.core, &bfs);
    int gate0_before = g.gates[0].outer;
    graph_rotate_gates(&g, changes);
    CHECK(!graph_has_edge(&g, gate0_before, changes[0].old_inner),
          "old gate edge still in adjacency list");
    bfs_path(&g, start, g.core, &astar);
    bool uses_old_gate = false;
    for (int i = 0; i < astar.length; i++)
        if (astar.nodes[i] == gate0_before && i + 1 < astar.length &&
            astar.nodes[i + 1] == changes[0].old_inner)
            uses_old_gate = true;
    CHECK(!uses_old_gate, "BFS still routes through the rotated-away gate");

    graph_free(&g);
}

static void test_union_find(void)
{
    UnionFind uf;
    printf("union-find: unions, ranks, path compression\n");
    uf_init(&uf, 8);
    CHECK(uf.sets == 8, "expected 8 sets");
    CHECK(uf_union(&uf, 0, 1), "union 0-1");
    CHECK(uf_union(&uf, 2, 3), "union 2-3");
    CHECK(uf_union(&uf, 1, 3), "union 1-3");
    CHECK(!uf_union(&uf, 0, 2), "0 and 2 already joined");
    CHECK(uf.sets == 5, "expected 5 sets, got %d", uf.sets);
    CHECK(uf_same(&uf, 0, 3) && !uf_same(&uf, 0, 4), "membership");

    /* Chain of unions; after one find every node points straight at the root. */
    uf_init(&uf, 16);
    for (int i = 0; i + 1 < 16; i++)
        uf_union(&uf, i, i + 1);
    int root = uf_find(&uf, 15);
    for (int i = 0; i < 16; i++) {
        uf_find(&uf, i);
        CHECK(uf.parent[i] == root, "node %d not compressed onto root", i);
    }
    CHECK(uf.rank[root] <= 4, "rank %d too high for 16 elements", uf.rank[root]);
}

/* Direction keys must resolve to exactly the edges in the adjacency list. */
static void test_move_directions(void)
{
    static Graph g;
    GateChange changes[RING_COUNT];
    printf("movement: direction keys map onto real edges\n");
    graph_build(&g, 400, 400, 350, 3);

    for (int round = 0; round < 50; round++) {
        for (int id = 0; id < g.node_count; id++) {
            const Node *n = &g.nodes[id];
            if (n->ring == RING_COUNT)
                continue; /* the core: game over, no moves */
            int cw  = entity_neighbor(&g, id, DIR_CW);
            int ccw = entity_neighbor(&g, id, DIR_CCW);
            int in  = entity_neighbor(&g, id, DIR_IN);
            int out = entity_neighbor(&g, id, DIR_OUT);

            CHECK(cw == graph_node_at(&g, n->ring, n->index + 1), "CW from %d", id);
            CHECK(ccw == graph_node_at(&g, n->ring, n->index - 1), "CCW from %d", id);
            CHECK((in >= 0) == (g.gates[n->ring].outer == id), "IN from %d", id);
            if (in >= 0)
                CHECK(in == g.gates[n->ring].inner, "IN from %d wrong node", id);
            bool has_out = n->ring > 0 && g.gates[n->ring - 1].inner == id;
            CHECK((out >= 0) == has_out, "OUT from %d", id);
        }
        graph_rotate_gates(&g, changes);
    }
    graph_free(&g);
}

/* Walk the player along the BFS route, ignoring cooldowns. */
static void force_step(Game *g)
{
    g->player.cooldown = 0.0f;
    game_move_player_to(g, g->route.nodes[1]);
}

static void test_rotation_rules(void)
{
    static Game g;
    printf("game rules: rotation every K crossings, enemy replans, captures\n");

    for (int k = 1; k <= 4; k++) {
        game_init(&g, 11, k, 400, 400, 350, NULL);
        g.enemy_wake = 1e9f; /* keep the enemy still */
        int expected_rotations = 0, crossings_seen = 0;

        while (g.state == STATE_PLAYING && g.route.length >= 2) {
            int replans_before = g.enemy.replans;
            int rotations_before = g.rotations;
            force_step(&g);

            if (g.crossings != crossings_seen) {
                crossings_seen = g.crossings;
                if (crossings_seen % k == 0 && g.state == STATE_PLAYING)
                    expected_rotations++;
            }
            CHECK(g.rotations == expected_rotations,
                  "K=%d: %d rotations after %d crossings", k, g.rotations, g.crossings);
            if (g.rotations > rotations_before)
                CHECK(g.enemy.replans > replans_before, "no A* replan on rotation");
            CHECK(graph_validate(&g.graph), "graph invalid mid-game");
        }
        CHECK(g.state == STATE_WON, "K=%d: player did not reach the core", k);
        game_free(&g);
    }

    /* Capture mode: gates the player crosses must stay put forever after. */
    game_init(&g, 5, 1, 400, 400, 350, NULL);
    g.enemy_wake = 1e9f;
    game_toggle_capture(&g);
    Gate captured[RING_COUNT];
    int captured_count = 0;
    while (g.state == STATE_PLAYING && g.route.length >= 2) {
        int from = g.player.node;
        force_step(&g);
        int from_ring = g.graph.nodes[from].ring;
        if (g.graph.nodes[g.player.node].ring > from_ring) {
            captured[from_ring] = (Gate){ from, g.player.node, true };
            captured_count = from_ring + 1;
        }
        for (int r = 0; r < captured_count; r++) {
            CHECK(g.graph.gates[r].outer == captured[r].outer &&
                  g.graph.gates[r].inner == captured[r].inner,
                  "captured gate %d moved", r);
            CHECK(uf_same(&g.territory, 0, r + 1), "ring %d not in territory", r + 1);
        }
    }
    CHECK(g.state == STATE_WON, "capture run did not win");
    game_free(&g);
}

/* BFS distance maps must agree with single-target BFS on every pair. */
static void test_bfs_distances(void)
{
    static Graph g;
    static Path p;
    GateChange changes[RING_COUNT];
    int dist[MAX_NODES];
    printf("bfs distances: distance maps vs single-pair BFS\n");
    graph_build(&g, 400, 400, 350, 9);
    for (int round = 0; round < 20; round++) {
        for (int s = 0; s < g.node_count; s++) {
            bfs_distances(&g, s, dist);
            for (int t = 0; t < g.node_count; t++) {
                bfs_path(&g, s, t, &p);
                CHECK(dist[t] == p.length - 1, "dist %d->%d = %d, BFS says %d",
                      s, t, dist[t], p.length - 1);
            }
        }
        graph_rotate_gates(&g, changes);
    }
    graph_free(&g);
}

/* Which direction key leads from `from` to its neighbour `to`, or -1. */
static int dir_towards(const Graph *g, int from, int to)
{
    for (int d = DIR_CCW; d <= DIR_OUT; d++)
        if (entity_neighbor(g, from, (MoveDir)d) == to)
            return d;
    return -1;
}

/*
 * Play whole games to sanity-check difficulty:
 *   solo Pandava  � BFS-guided AI runner vs the A* AI chaser
 *   solo Kaurava  � AI runner vs a scripted "human" chaser that presses the
 *                   key along its A* scout route (tests the human-chaser API)
 */
static void simulate(const char *label, bool scripted_chaser)
{
    static Game g;
    int wins = 0, games = 300, rotations = 0;
    float win_time = 0.0f;
    const float dt = 1.0f / 60.0f;

    for (int seed = 1; seed <= games; seed++) {
        GameSetup setup = { "Runner", "Chaser", true, !scripted_chaser, false };
        game_init(&g, (uint32_t)seed, DEFAULT_ROTATION_K, 400, 400, 350, &setup);
        for (int frame = 0; frame < 60 * 120 && g.state == STATE_PLAYING; frame++) {
            if (scripted_chaser && g.enemy.path.length > 1) {
                int d = dir_towards(&g.graph, g.enemy.node, g.enemy.path.nodes[1]);
                if (d >= 0)
                    game_try_move_chaser(&g, (MoveDir)d);
            }
            game_update(&g, dt);
            CHECK(g.player.node >= 0 && g.enemy.node >= 0, "entity off graph");
        }
        CHECK(g.state != STATE_PLAYING, "%s seed %d: game never ended", label, seed);
        if (g.state == STATE_WON) {
            wins++;
            win_time += g.elapsed;
        }
        rotations += g.rotations;
        game_free(&g);
    }
    printf("balance (%s): Pandava AI won %d/%d (avg %.1fs), %.1f rotations/game\n",
           label, wins, games, wins ? win_time / wins : 0.0f, (float)rotations / games);
}

static void test_human_chaser_rules(void)
{
    static Game g;
    printf("human chaser: waits for wake-up, obeys cooldown and edges\n");
    GameSetup setup = { "Runner", "Chaser", false, false, false };
    game_init(&g, 3, DEFAULT_ROTATION_K, 400, 400, 350, &setup);
    int start = g.enemy.node;
    CHECK(!game_try_move_chaser(&g, DIR_CW), "chaser moved before waking");
    g.enemy_wake = 0.0f;
    CHECK(game_try_move_chaser(&g, DIR_CW), "chaser could not move");
    CHECK(g.enemy.node == graph_node_at(&g.graph, 0, g.graph.nodes[start].index + 1),
          "chaser moved to the wrong node");
    CHECK(!game_try_move_chaser(&g, DIR_CW), "chaser ignored its cooldown");
    int before = g.enemy.node;
    for (int i = 0; i < 200; i++)
        game_update(&g, 1.0f / 60.0f);
    CHECK(g.enemy.node == before, "human chaser moved without input");
    game_free(&g);
}

/*
 * Online play: the host's game referees moves; the guest's game replays the
 * accepted moves with game_force_move. Both must stay identical: same nodes,
 * same gates after every rotation, same outcome. Random inputs from both
 * sides, 200 battles, with K = 1..3 so rotations happen often.
 */
static void test_online_lockstep(void)
{
    static Game host, guest;
    uint32_t rng = 99;
    int battles = 200, rotations = 0;
    printf("online lockstep: guest replaying host moves stays identical\n");

    for (int b = 0; b < battles; b++) {
        GameSetup setup = { "Host", "Guest", false, false, false };
        int k = 1 + b % 3;
        game_init(&host, 1000u + (uint32_t)b, k, 400, 400, 350, &setup);
        game_init(&guest, 1000u + (uint32_t)b, k, 400, 400, 350, &setup);

        for (int frame = 0; frame < 60 * 60 && host.state == STATE_PLAYING; frame++) {
            int runner_before = host.player.node, chaser_before = host.enemy.node;
            rng = rng * 1664525u + 1013904223u;
            game_try_move(&host, (MoveDir)((rng >> 8) % 4));
            rng = rng * 1664525u + 1013904223u;
            game_try_move_chaser(&host, (MoveDir)((rng >> 8) % 4));
            game_update(&host, 1.0f / 60.0f);
            game_update(&guest, 1.0f / 60.0f);

            /* What online_broadcast_moves sends, in the same order. */
            if (host.player.node != runner_before)
                CHECK(game_force_move(&guest, 0, host.player.node), "guest rejected runner move");
            if (host.enemy.node != chaser_before)
                CHECK(game_force_move(&guest, 1, host.enemy.node), "guest rejected chaser move");

            CHECK(guest.player.node == host.player.node && guest.enemy.node == host.enemy.node,
                  "battle %d frame %d: positions differ", b, frame);
            CHECK(guest.rotations == host.rotations, "battle %d: rotations differ", b);
            for (int r = 0; r < RING_COUNT; r++)
                CHECK(guest.graph.gates[r].outer == host.graph.gates[r].outer &&
                      guest.graph.gates[r].inner == host.graph.gates[r].inner,
                      "battle %d: gate %d differs", b, r);
        }
        CHECK(guest.state == host.state, "battle %d: outcomes differ", b);
        rotations += host.rotations;
        game_free(&host);
        game_free(&guest);
    }
    printf("  %d battles, %d rotations, all in step\n", battles, rotations);
}

int main(void)
{
    game_console_log = false; /* keep test output readable */

    test_pathfinding();
    test_union_find();
    test_move_directions();
    test_rotation_rules();
    test_bfs_distances();
    test_human_chaser_rules();
    test_online_lockstep();
    simulate("vs A* chaser", false);
    simulate("vs scripted human chaser", true);

    printf(failures ? "\n%d CHECK(S) FAILED\n" : "\nALL TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
