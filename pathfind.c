/*
 * pathfind.c — BFS and A* on the adjacency-list graph.
 * No rendering code and no ring-specific logic: only nodes and edges.
 */
#include "pathfind.h"

#include <math.h>
#include <string.h>

#define NO_PARENT (-1)

float node_distance(const Graph *g, int a, int b)
{
    float dx = g->nodes[a].x - g->nodes[b].x;
    float dy = g->nodes[a].y - g->nodes[b].y;
    return sqrtf(dx * dx + dy * dy);
}

/*
 * Walk the parent links back from goal to start, then reverse them so the
 * path reads start -> goal.
 */
static void build_path(const int *parent, int start, int goal, Path *out)
{
    int reversed[MAX_NODES];
    int n = 0;

    for (int v = goal; v != NO_PARENT; v = parent[v]) {
        reversed[n++] = v;
        if (v == start)
            break;
    }
    for (int i = 0; i < n; i++)
        out->nodes[i] = reversed[n - 1 - i];
    out->length = n;
}

static void path_reset(Path *out)
{
    out->length   = 0;
    out->cost     = 0.0f;
    out->expanded = 0;
    memset(out->visited, 0, sizeof out->visited);
}

/* ------------------------------------------------------------------------ */
/* Breadth-first search                                                     */
/*                                                                          */
/* A FIFO queue explores nodes in order of hop distance from the start, so  */
/* the first time the goal is dequeued we have a fewest-edges path.         */
/* Runs in O(V + E).                                                        */
/* ------------------------------------------------------------------------ */

bool bfs_path(const Graph *g, int start, int goal, Path *out)
{
    int  queue[MAX_NODES];  /* each node is enqueued at most once */
    int  head = 0, tail = 0;
    int  parent[MAX_NODES];
    bool discovered[MAX_NODES] = { false };

    path_reset(out);
    for (int i = 0; i < g->node_count; i++)
        parent[i] = NO_PARENT;

    discovered[start] = true;
    queue[tail++] = start;

    while (head < tail) {
        int v = queue[head++];          /* dequeue the oldest node */
        out->visited[v] = true;
        out->expanded++;

        if (v == goal) {
            build_path(parent, start, goal, out);
            out->cost = (float)(out->length - 1);
            return true;
        }

        /* Enqueue every neighbour not seen before; remember who found it. */
        for (const Edge *e = g->nodes[v].adj; e; e = e->next) {
            if (!discovered[e->to]) {
                discovered[e->to] = true;
                parent[e->to]     = v;
                queue[tail++]     = e->to;
            }
        }
    }
    return false;
}

/*
 * Same traversal as bfs_path, but it never stops early: every reachable node
 * gets its hop distance, i.e. dist[neighbour] = dist[v] + 1 when first seen.
 */
void bfs_distances(const Graph *g, int start, int *dist)
{
    int queue[MAX_NODES];
    int head = 0, tail = 0;

    for (int i = 0; i < g->node_count; i++)
        dist[i] = -1;
    dist[start] = 0;
    queue[tail++] = start;

    while (head < tail) {
        int v = queue[head++];
        for (const Edge *e = g->nodes[v].adj; e; e = e->next) {
            if (dist[e->to] < 0) {
                dist[e->to]   = dist[v] + 1;
                queue[tail++] = e->to;
            }
        }
    }
}

/* ------------------------------------------------------------------------ */
/* Binary min-heap keyed on f-score (the A* open set)                       */
/* ------------------------------------------------------------------------ */

/*
 * A node may be pushed again when a cheaper route to it is found; the stale
 * copy is skipped when popped ("lazy deletion"). Each directed edge can push
 * at most once, so this bound is generous for our graphs.
 */
#define HEAP_CAPACITY (MAX_NODES * 8)

typedef struct {
    float f;
    int   node;
} HeapItem;

typedef struct {
    HeapItem items[HEAP_CAPACITY];
    int      size;
} MinHeap;

static void heap_swap(HeapItem *a, HeapItem *b)
{
    HeapItem t = *a;
    *a = *b;
    *b = t;
}

static bool heap_push(MinHeap *h, int node, float f)
{
    if (h->size == HEAP_CAPACITY)
        return false;

    /* Place at the end, then sift up while smaller than the parent. */
    int i = h->size++;
    h->items[i].f    = f;
    h->items[i].node = node;
    while (i > 0) {
        int parent = (i - 1) / 2;
        if (h->items[parent].f <= h->items[i].f)
            break;
        heap_swap(&h->items[parent], &h->items[i]);
        i = parent;
    }
    return true;
}

static HeapItem heap_pop(MinHeap *h)
{
    HeapItem top = h->items[0];

    /* Move the last item to the root, then sift down to the smaller child. */
    h->items[0] = h->items[--h->size];
    int i = 0;
    for (;;) {
        int left = 2 * i + 1, right = left + 1, smallest = i;
        if (left < h->size && h->items[left].f < h->items[smallest].f)
            smallest = left;
        if (right < h->size && h->items[right].f < h->items[smallest].f)
            smallest = right;
        if (smallest == i)
            break;
        heap_swap(&h->items[i], &h->items[smallest]);
        i = smallest;
    }
    return top;
}

/* ------------------------------------------------------------------------ */
/* A* search                                                                */
/*                                                                          */
/*   g(n) = cheapest known cost from start to n                             */
/*   h(n) = straight-line distance from n to goal (admissible heuristic)    */
/*   f(n) = g(n) + h(n), the priority in the open set                       */
/*                                                                          */
/* Repeatedly expand the open node with the lowest f. Because edge costs    */
/* are themselves Euclidean lengths, h is consistent (triangle inequality), */
/* so a node's g is final once it is expanded ("closed").                   */
/* ------------------------------------------------------------------------ */

bool astar_path(const Graph *g, int start, int goal, Path *out)
{
    static MinHeap open;             /* static: keeps ~4 KB off the stack */
    float gscore[MAX_NODES];
    int   parent[MAX_NODES];
    bool  closed[MAX_NODES] = { false };

    path_reset(out);
    for (int i = 0; i < g->node_count; i++) {
        gscore[i] = INFINITY;
        parent[i] = NO_PARENT;
    }

    open.size     = 0;
    gscore[start] = 0.0f;
    heap_push(&open, start, node_distance(g, start, goal));

    while (open.size > 0) {
        int v = heap_pop(&open).node;
        if (closed[v])
            continue;               /* stale duplicate: already expanded */
        closed[v] = true;
        out->visited[v] = true;
        out->expanded++;

        if (v == goal) {
            build_path(parent, start, goal, out);
            out->cost = gscore[goal];
            return true;
        }

        /* Relax each edge: keep the neighbour's route through v if cheaper. */
        for (const Edge *e = g->nodes[v].adj; e; e = e->next) {
            int w = e->to;
            if (closed[w])
                continue;
            float tentative = gscore[v] + node_distance(g, v, w);
            if (tentative < gscore[w]) {
                gscore[w] = tentative;
                parent[w] = v;
                heap_push(&open, w, tentative + node_distance(g, w, goal));
            }
        }
    }
    return false;
}
