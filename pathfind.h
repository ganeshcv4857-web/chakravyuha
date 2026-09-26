/*
 * pathfind.h — Breadth-first search and A* over the Graph adjacency lists.
 *
 * Both searches are generic: they only use node_count, each node's
 * adjacency list and each node's (x, y) position. They know nothing about
 * rings, gates or rendering, so they work on any graph built with graph.h.
 */
#ifndef PATHFIND_H
#define PATHFIND_H

#include "graph.h"

typedef struct {
    int   nodes[MAX_NODES];   /* nodes[0] = start ... nodes[length-1] = goal */
    int   length;             /* number of nodes on the path; 0 = unreachable */
    float cost;               /* BFS: hop count, A*: summed edge lengths */
    int   expanded;           /* how many nodes the search removed from its frontier */
    bool  visited[MAX_NODES]; /* nodes the search expanded (for the debug overlay) */
} Path;

/* Unweighted shortest path (fewest edges). Returns true if goal is reachable. */
bool bfs_path(const Graph *g, int start, int goal, Path *out);

/*
 * Weighted shortest path. Edge cost is the Euclidean length between node
 * positions; the heuristic is the straight-line distance to the goal, which
 * never overestimates, so the result is optimal. Returns true if reachable.
 */
bool astar_path(const Graph *g, int start, int goal, Path *out);

/*
 * Hop distance from `start` to every node (BFS without a goal).
 * dist[v] = -1 if v is unreachable. `dist` must hold node_count entries.
 */
void bfs_distances(const Graph *g, int start, int *dist);

/* Straight-line distance between two nodes' positions. */
float node_distance(const Graph *g, int a, int b);

#endif /* PATHFIND_H */
