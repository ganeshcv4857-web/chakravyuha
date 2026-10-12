/*
 * graph.c — Adjacency-list graph for the Chakravyuha formation.
 * See graph.h for the model. No rendering code lives here.
 */
#include "graph.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Outermost ring first; inner rings shrink so the formation tightens. */
const int RING_SIZES[RING_COUNT] = { 12, 10, 8, 6, 4 };

/* ------------------------------------------------------------------------ */
/* Pseudo-random numbers (xorshift32) — deterministic for a given seed.     */
/* ------------------------------------------------------------------------ */

static uint32_t rng_next(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

/* ------------------------------------------------------------------------ */
/* Adjacency list primitives                                                */
/* ------------------------------------------------------------------------ */

/* Append one directed entry a -> b to a's list. */
static void list_append(Node *a, int b, EdgeKind kind)
{
    Edge *e = malloc(sizeof *e);
    if (!e) {
        fprintf(stderr, "graph: out of memory\n");
        exit(1);
    }
    e->to   = b;
    e->kind = kind;
    e->next = NULL;

    /* Walk to the tail so lists keep insertion order (lists are tiny). */
    Edge **link = &a->adj;
    while (*link)
        link = &(*link)->next;
    *link = e;
    a->degree++;
}

/* Unlink and free the directed entry a -> b. Returns false if absent. */
static bool list_remove(Node *a, int b)
{
    for (Edge **link = &a->adj; *link; link = &(*link)->next) {
        if ((*link)->to == b) {
            Edge *dead = *link;
            *link = dead->next;
            free(dead);
            a->degree--;
            return true;
        }
    }
    return false;
}

bool graph_has_edge(const Graph *g, int a, int b)
{
    for (const Edge *e = g->nodes[a].adj; e; e = e->next)
        if (e->to == b)
            return true;
    return false;
}

/* Undirected edge = one entry in each endpoint's list. */
void graph_add_edge(Graph *g, int a, int b, EdgeKind kind)
{
    if (a == b || graph_has_edge(g, a, b))
        return;
    list_append(&g->nodes[a], b, kind);
    list_append(&g->nodes[b], a, kind);
}

bool graph_remove_edge(Graph *g, int a, int b)
{
    bool ab = list_remove(&g->nodes[a], b);
    bool ba = list_remove(&g->nodes[b], a);
    return ab && ba;
}

/* ------------------------------------------------------------------------ */
/* Construction                                                             */
/* ------------------------------------------------------------------------ */

int graph_node_at(const Graph *g, int ring, int index)
{
    const Ring *r = &g->rings[ring];
    int i = ((index % r->count) + r->count) % r->count;
    return r->first + i;
}

void graph_layout(Graph *g, float cx, float cy, float outer_radius)
{
    for (int r = 0; r <= RING_COUNT; r++) {
        Ring *ring = &g->rings[r];
        /* Evenly spaced radii: ring 0 at outer_radius, core at the centre. */
        ring->radius = outer_radius * (float)(RING_COUNT - r) / RING_COUNT;

        for (int i = 0; i < ring->count; i++) {
            Node *n = &g->nodes[ring->first + i];
            /* angle = 2*pi*index/count, rotated so index 0 is at the top. */
            double angle = 2.0 * M_PI * i / ring->count - M_PI / 2.0;
            n->x = cx + ring->radius * (float)cos(angle);
            n->y = cy + ring->radius * (float)sin(angle);
        }
    }
}

/*
 * Index on ring r+1 whose angle is closest to `outer_index` on ring r.
 * Both rings use angle = 2*pi*i/n, so this is round(i * n_in / n_out),
 * done in integers to avoid floating-point ties.
 */
static int inner_index_for(const Graph *g, int ring, int outer_index)
{
    int n_out = g->rings[ring].count;
    int n_in  = g->rings[ring + 1].count;
    return ((2 * outer_index * n_in + n_out) / (2 * n_out)) % n_in;
}

/* Insert gate `ring` at `outer_index` on that ring. */
static void place_gate(Graph *g, int ring, int outer_index)
{
    Gate *gate  = &g->gates[ring];
    gate->outer = graph_node_at(g, ring, outer_index);
    gate->inner = graph_node_at(g, ring + 1, inner_index_for(g, ring, outer_index));
    graph_add_edge(g, gate->outer, gate->inner, EDGE_GATE);
}

void graph_build(Graph *g, float cx, float cy, float outer_radius, uint32_t seed)
{
    g->node_count = 0;
    g->rng = seed ? seed : 0x9E3779B9u; /* xorshift must not start at 0 */

    /* 1. Allocate node ids ring by ring, then the core. */
    for (int r = 0; r <= RING_COUNT; r++) {
        Ring *ring  = &g->rings[r];
        ring->first = g->node_count;
        ring->count = (r < RING_COUNT) ? RING_SIZES[r] : 1;

        for (int i = 0; i < ring->count; i++) {
            Node *n   = &g->nodes[g->node_count];
            n->id     = g->node_count;
            n->ring   = r;
            n->index  = i;
            n->adj    = NULL;
            n->degree = 0;
            g->node_count++;
        }
    }
    g->core = g->rings[RING_COUNT].first;

    graph_layout(g, cx, cy, outer_radius);

    /* 2. Cycle edges: join each node to its clockwise neighbour. */
    for (int r = 0; r < RING_COUNT; r++) {
        for (int i = 0; i < g->rings[r].count; i++) {
            graph_add_edge(g, graph_node_at(g, r, i), graph_node_at(g, r, i + 1),
                           EDGE_RING);
        }
    }

    /* 3. One gate edge per ring, at a random position on that ring. */
    for (int r = 0; r < RING_COUNT; r++) {
        g->gates[r].locked = false;
        place_gate(g, r, (int)(rng_next(&g->rng) % g->rings[r].count));
    }
}

void graph_free(Graph *g)
{
    for (int i = 0; i < g->node_count; i++) {
        Edge *e = g->nodes[i].adj;
        while (e) {
            Edge *next = e->next;
            free(e);
            e = next;
        }
        g->nodes[i].adj    = NULL;
        g->nodes[i].degree = 0;
    }
}

/* ------------------------------------------------------------------------ */
/* Gate rotation — a real edge-list mutation                                */
/* ------------------------------------------------------------------------ */

int graph_rotate_gates(Graph *g, GateChange *out)
{
    int moved = 0;

    for (int r = 0; r < RING_COUNT; r++) {
        Gate *gate = &g->gates[r];
        if (gate->locked)
            continue;

        int count = g->rings[r].count;
        int old_index = g->nodes[gate->outer].index;

        /* Offset in [1, count-1] guarantees the gate lands on a new node. */
        int offset = 1 + (int)(rng_next(&g->rng) % (uint32_t)(count - 1));

        GateChange *c = &out[moved++];
        c->ring      = r;
        c->old_outer = gate->outer;
        c->old_inner = gate->inner;

        /* Delete the old gate edge from both adjacency lists ... */
        graph_remove_edge(g, gate->outer, gate->inner);
        /* ... and insert the new one. */
        place_gate(g, r, old_index + offset);

        c->new_outer = gate->outer;
        c->new_inner = gate->inner;
    }
    return moved;
}

/* ------------------------------------------------------------------------ */
/* Verification and debug output                                            */
/* ------------------------------------------------------------------------ */

static EdgeKind edge_kind(const Graph *g, int a, int b)
{
    for (const Edge *e = g->nodes[a].adj; e; e = e->next)
        if (e->to == b)
            return e->kind;
    return EDGE_RING; /* unreachable when called on an existing edge */
}

bool graph_validate(const Graph *g)
{
    bool ok = true;
    int gate_edges[RING_COUNT] = { 0 };

#define FAIL(...) do { fprintf(stderr, "  INVALID: " __VA_ARGS__); ok = false; } while (0)

    for (int a = 0; a < g->node_count; a++) {
        const Node *na = &g->nodes[a];
        int listed = 0, ring_edges = 0;

        for (const Edge *e = na->adj; e; e = e->next, listed++) {
            int b = e->to;
            if (b < 0 || b >= g->node_count || b == a) {
                FAIL("n%d has bad neighbour %d\n", a, b);
                continue;
            }
            const Node *nb = &g->nodes[b];

            /* Undirected: b must list a back, with the same edge kind. */
            if (!graph_has_edge(g, b, a) || edge_kind(g, b, a) != e->kind)
                FAIL("n%d -> n%d has no matching reverse edge\n", a, b);

            /* No duplicate neighbours. */
            for (const Edge *f = e->next; f; f = f->next)
                if (f->to == b)
                    FAIL("n%d lists n%d twice\n", a, b);

            if (e->kind == EDGE_RING) {
                ring_edges++;
                int n = g->rings[na->ring].count;
                bool adjacent = nb->ring == na->ring &&
                                ((nb->index - na->index + n) % n == 1 ||
                                 (na->index - nb->index + n) % n == 1);
                if (!adjacent)
                    FAIL("ring edge n%d-n%d joins non-adjacent nodes\n", a, b);
            } else {
                int lo = na->ring < nb->ring ? na->ring : nb->ring;
                int hi = na->ring < nb->ring ? nb->ring : na->ring;
                if (hi - lo != 1) {
                    FAIL("gate edge n%d-n%d skips rings\n", a, b);
                } else if (a < b) {
                    gate_edges[lo]++; /* count each undirected edge once */
                }
            }
        }

        if (listed != na->degree)
            FAIL("n%d degree %d but list has %d entries\n", a, na->degree, listed);
        if (na->ring < RING_COUNT && ring_edges != 2)
            FAIL("n%d has %d ring edges (expected 2)\n", a, ring_edges);
    }

    for (int r = 0; r < RING_COUNT; r++) {
        const Gate *gate = &g->gates[r];
        if (gate_edges[r] != 1)
            FAIL("rings %d/%d joined by %d gate edges (expected 1)\n",
                 r, r + 1, gate_edges[r]);
        if (g->nodes[gate->outer].ring != r || g->nodes[gate->inner].ring != r + 1)
            FAIL("gate %d table entry has wrong rings\n", r);
        if (!graph_has_edge(g, gate->outer, gate->inner))
            FAIL("gate %d (n%d-n%d) missing from adjacency lists\n",
                 r, gate->outer, gate->inner);
    }

#undef FAIL
    return ok;
}

/* "R2.5" = ring 2 (1-based, as shown in game), index 5; or "CORE". */
static const char *label(const Graph *g, int id, char *buf, size_t size)
{
    const Node *n = &g->nodes[id];
    if (n->ring == RING_COUNT)
        snprintf(buf, size, "CORE");
    else
        snprintf(buf, size, "R%d.%d", n->ring + 1, n->index);
    return buf;
}

void graph_print(const Graph *g)
{
    char a[16], b[16];

    for (int r = 0; r <= RING_COUNT; r++) {
        const Ring *ring = &g->rings[r];
        if (r < RING_COUNT)
            printf("Ring %d: %d nodes (ids %d..%d), radius %.0f, gate %s <-> %s\n",
                   r + 1, ring->count, ring->first, ring->first + ring->count - 1,
                   ring->radius,
                   label(g, g->gates[r].outer, a, sizeof a),
                   label(g, g->gates[r].inner, b, sizeof b));
        else
            printf("Core: node id %d\n", ring->first);

        for (int i = 0; i < ring->count; i++) {
            const Node *n = &g->nodes[ring->first + i];
            printf("  n%-2d %-5s (%6.1f,%6.1f) deg %d :", n->id,
                   label(g, n->id, a, sizeof a), n->x, n->y, n->degree);
            for (const Edge *e = n->adj; e; e = e->next)
                printf("  -> n%-2d %-5s%s", e->to, label(g, e->to, b, sizeof b),
                       e->kind == EDGE_GATE ? " [GATE]" : "");
            printf("\n");
        }
        printf("\n");
    }
}
