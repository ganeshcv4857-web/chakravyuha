/*
 * graph_dump.c — Phase 1 check: build the formation, print every adjacency
 * list, rotate the gates, and confirm the edge lists really changed.
 *
 *   make dump && ./graph_dump [seed]
 */
#include "../graph.h"

#include <stdio.h>
#include <stdlib.h>

static int count_edges(const Graph *g, EdgeKind kind)
{
    int total = 0;
    for (int i = 0; i < g->node_count; i++)
        for (const Edge *e = g->nodes[i].adj; e; e = e->next)
            if (e->kind == kind && i < e->to)
                total++;
    return total;
}

static void print_changes(const Graph *g, const GateChange *c, int n)
{
    for (int i = 0; i < n; i++) {
        const Node *oo = &g->nodes[c[i].old_outer], *oi = &g->nodes[c[i].old_inner];
        const Node *no = &g->nodes[c[i].new_outer], *ni = &g->nodes[c[i].new_inner];
        printf("  gate %d: n%d(R%d.%d)-n%d  ->  n%d(R%d.%d)-n%d   "
               "old edge present: %s, new edge present: %s\n",
               c[i].ring + 1, oo->id, oo->ring + 1, oo->index, oi->id,
               no->id, no->ring + 1, no->index, ni->id,
               graph_has_edge(g, oo->id, oi->id) ? "YES" : "no",
               graph_has_edge(g, no->id, ni->id) ? "YES" : "no");
    }
}

int main(int argc, char **argv)
{
    uint32_t seed = argc > 1 ? (uint32_t)strtoul(argv[1], NULL, 10) : 2026;
    static Graph g;
    GateChange changes[RING_COUNT];

    graph_build(&g, 400.0f, 400.0f, 350.0f, seed);

    printf("=== Chakravyuha graph (seed %u) ===\n", (unsigned)seed);
    printf("%d nodes, %d ring edges, %d gate edges\n\n",
           g.node_count, count_edges(&g, EDGE_RING), count_edges(&g, EDGE_GATE));
    graph_print(&g);
    printf("validate: %s\n\n", graph_validate(&g) ? "OK" : "FAILED");

    printf("=== Rotation 1 ===\n");
    int moved = graph_rotate_gates(&g, changes);
    print_changes(&g, changes, moved);
    printf("\n");
    graph_print(&g);
    printf("validate: %s\n\n", graph_validate(&g) ? "OK" : "FAILED");

    /* Stress: many rotations must keep every invariant and always move. */
    int bad = 0;
    for (int round = 0; round < 10000; round++) {
        moved = graph_rotate_gates(&g, changes);
        for (int i = 0; i < moved; i++)
            if (changes[i].old_outer == changes[i].new_outer)
                bad++;
        if (!graph_validate(&g))
            bad++;
    }
    printf("=== 10000 further rotations: %s (%d problems) ===\n",
           bad ? "FAILED" : "OK", bad);

    graph_free(&g);
    return bad ? 1 : 0;
}
