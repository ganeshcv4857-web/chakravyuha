/*
 * entity.h — Player and enemy state. Entities always sit ON a graph node;
 * they move only along an existing edge, one node at a time.
 *
 * prev_node/move_t describe the short slide animation from the previous
 * node. They are purely visual: the logical position is always `node`.
 */
#ifndef ENTITY_H
#define ENTITY_H

#include "graph.h"
#include "pathfind.h"

/* Player controls, relative to the ring the player is on. */
typedef enum {
    DIR_CCW, /* counter-clockwise neighbour on the ring */
    DIR_CW,  /* clockwise neighbour on the ring */
    DIR_IN,  /* through a gate to the next ring inward */
    DIR_OUT  /* through a gate to the next ring outward */
} MoveDir;

typedef struct {
    int   node;       /* current node id (the logical position) */
    int   prev_node;  /* node it last moved from (for the slide animation) */
    float move_t;     /* slide progress 0..1 (visual only) */
    float slide_time; /* seconds a slide lasts */
    float cooldown;   /* seconds until the next move is allowed */
    int   moves;      /* total steps taken */

    /* Enemy AI only */
    Path  path;       /* current A* route, path.nodes[0] = where it was planned */
    int   path_pos;   /* index into path.nodes of the node we stand on */
    int   target;     /* node the route leads to (the player, when planned) */
    int   replans;    /* how many times A* has been run */
} Entity;

void entity_init(Entity *e, int node, float slide_time);

/* Neighbour of `node` in direction `dir` from the adjacency list, or -1. */
int entity_neighbor(const Graph *g, int node, MoveDir dir);

/* Step to `to` if an edge node->to exists. Returns true if it moved. */
bool entity_move(Entity *e, const Graph *g, int to);

/* Advance timers and the slide animation. */
void entity_tick(Entity *e, float dt);

/* Enemy: run A* from the enemy's node to `target` and follow the result. */
void enemy_replan(Entity *e, const Graph *g, int target);

/* Enemy: next node on the current route, or -1 if at the end / no route. */
int enemy_next_node(const Entity *e);

/* Enemy: true once it stands on the node its route was planned to. */
bool enemy_reached_target(const Entity *e);

#endif /* ENTITY_H */
