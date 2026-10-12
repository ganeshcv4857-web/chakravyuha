/*
 * entity.c — Node-snapped movement for the player and the enemy.
 */
#include "entity.h"

void entity_init(Entity *e, int node, float slide_time)
{
    e->node        = node;
    e->prev_node   = node;
    e->move_t      = 1.0f;
    e->slide_time  = slide_time;
    e->cooldown    = 0.0f;
    e->moves       = 0;
    e->path.length = 0;
    e->path_pos    = 0;
    e->target      = -1;
    e->replans     = 0;
}

/*
 * Resolve a direction key to a neighbour by scanning the adjacency list.
 * Only edges that actually exist can be chosen, so the player can never
 * leave the graph or use a gate that has rotated away.
 */
int entity_neighbor(const Graph *g, int node, MoveDir dir)
{
    const Node *n = &g->nodes[node];
    int count = g->rings[n->ring].count;

    for (const Edge *e = n->adj; e; e = e->next) {
        const Node *m = &g->nodes[e->to];
        switch (dir) {
        case DIR_CW:
            if (e->kind == EDGE_RING && m->index == (n->index + 1) % count)
                return m->id;
            break;
        case DIR_CCW:
            if (e->kind == EDGE_RING && m->index == (n->index + count - 1) % count)
                return m->id;
            break;
        case DIR_IN:
            if (e->kind == EDGE_GATE && m->ring > n->ring)
                return m->id;
            break;
        case DIR_OUT:
            if (e->kind == EDGE_GATE && m->ring < n->ring)
                return m->id;
            break;
        }
    }
    return -1;
}

bool entity_move(Entity *e, const Graph *g, int to)
{
    if (to < 0 || !graph_has_edge(g, e->node, to))
        return false;
    e->prev_node = e->node;
    e->node      = to;
    e->move_t    = 0.0f;
    e->moves++;
    return true;
}

void entity_tick(Entity *e, float dt)
{
    if (e->cooldown > 0.0f)
        e->cooldown -= dt;
    if (e->move_t < 1.0f) {
        e->move_t += dt / e->slide_time;
        if (e->move_t > 1.0f)
            e->move_t = 1.0f;
    }
}

void enemy_replan(Entity *e, const Graph *g, int target)
{
    astar_path(g, e->node, target, &e->path);
    e->path_pos = 0;
    e->target   = target;
    e->replans++;
}

int enemy_next_node(const Entity *e)
{
    if (e->path_pos + 1 >= e->path.length)
        return -1;
    return e->path.nodes[e->path_pos + 1];
}

bool enemy_reached_target(const Entity *e)
{
    return e->node == e->target;
}
