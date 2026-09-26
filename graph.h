/*
 * graph.h — Graph model for Chakravyuha.
 *
 * The formation is an undirected graph stored as an ADJACENCY LIST:
 * every node owns a singly linked list of Edge records, one per neighbour.
 *
 *   - Rings 0..RING_COUNT-1 (ring 0 = outermost) are CYCLE graphs:
 *     node i on a ring is joined to nodes (i-1) and (i+1) mod n.
 *   - One extra CORE node sits in the middle as the objective.
 *   - Consecutive rings are joined by exactly one GATE edge:
 *     gate r links a node on ring r to a node on ring r+1
 *     (gate RING_COUNT-1 links the innermost ring to the core).
 *
 * Gate rotation removes each gate edge from the adjacency lists and inserts
 * a new one at a different node, so any pathfinding run afterwards sees the
 * mutated graph.
 *
 * This module is pure C: no rendering calls. Node x/y positions are plain
 * geometry, used for drawing and as the A* heuristic.
 */
#ifndef GRAPH_H
#define GRAPH_H

#include <stdbool.h>
#include <stdint.h>

#define RING_COUNT 5
#define MAX_NODES  64

/* Nodes per ring, outermost first. The core is one extra node. */
extern const int RING_SIZES[RING_COUNT];

typedef enum {
    EDGE_RING, /* edge between neighbours on the same ring's cycle */
    EDGE_GATE  /* edge between ring r and ring r+1 (or the core)   */
} EdgeKind;

/* One entry in a node's adjacency list. */
typedef struct Edge {
    int          to;   /* id of the neighbouring node */
    EdgeKind     kind;
    struct Edge *next; /* next neighbour in this node's list */
} Edge;

typedef struct {
    int   id;     /* index into Graph.nodes */
    int   ring;   /* 0..RING_COUNT-1, or RING_COUNT for the core */
    int   index;  /* position around its ring, 0..ring size-1 */
    float x, y;   /* layout position (screen space) */
    Edge *adj;    /* head of adjacency list */
    int   degree; /* length of adj */
} Node;

/* Nodes of ring r are stored contiguously: ids first .. first+count-1. */
typedef struct {
    int   first;
    int   count;
    float radius;
} Ring;

/* Gate r is the one edge between ring r and ring r+1. */
typedef struct {
    int  outer;  /* node id on ring r */
    int  inner;  /* node id on ring r+1 (or the core) */
    bool locked; /* captured gates are skipped by rotation (stretch goal) */
} Gate;

/* Record of one gate moving, for logging and the rotation animation. */
typedef struct {
    int ring;
    int old_outer, old_inner;
    int new_outer, new_inner;
} GateChange;

typedef struct {
    Node     nodes[MAX_NODES];
    int      node_count;
    Ring     rings[RING_COUNT + 1]; /* last entry describes the core */
    Gate     gates[RING_COUNT];
    int      core;                  /* node id of the core */
    uint32_t rng;                   /* PRNG state for gate placement */
} Graph;

/* Build all rings, cycle edges and initial gates. `seed` picks gate spots. */
void graph_build(Graph *g, float cx, float cy, float outer_radius, uint32_t seed);

/* Free every adjacency list. The Graph itself is caller-owned. */
void graph_free(Graph *g);

/* Recompute node positions (e.g. after a window resize). */
void graph_layout(Graph *g, float cx, float cy, float outer_radius);

/* Adjacency list primitives. Both add/remove act on BOTH directions. */
void graph_add_edge(Graph *g, int a, int b, EdgeKind kind);
bool graph_remove_edge(Graph *g, int a, int b);
bool graph_has_edge(const Graph *g, int a, int b);

/*
 * Move every unlocked gate to a different node on its ring.
 * Writes one GateChange per moved gate into `out` (capacity RING_COUNT)
 * and returns how many gates moved.
 */
int graph_rotate_gates(Graph *g, GateChange *out);

/* Id of the node at `index` on `ring` (index wraps around). */
int graph_node_at(const Graph *g, int ring, int index);

/*
 * Check structural invariants (edge symmetry, ring cycles, exactly one gate
 * edge per ring pair, gate table matches edge lists). Returns true if all
 * hold; prints each violation to stderr.
 */
bool graph_validate(const Graph *g);

/* Print every node's adjacency list to stdout. */
void graph_print(const Graph *g);

#endif /* GRAPH_H */
