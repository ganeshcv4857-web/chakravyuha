/*
 * unionfind.c — Disjoint-set forest.
 */
#include "unionfind.h"

void uf_init(UnionFind *uf, int count)
{
    uf->count = count;
    uf->sets  = count;
    for (int i = 0; i < count; i++) {
        uf->parent[i] = i; /* every element starts as its own set */
        uf->rank[i]   = 0;
    }
}

/*
 * Find the root of x's set. Path compression: after locating the root,
 * point every node on the way directly at it, so later finds are ~O(1).
 */
int uf_find(UnionFind *uf, int x)
{
    int root = x;
    while (uf->parent[root] != root)
        root = uf->parent[root];

    while (uf->parent[x] != root) {
        int next = uf->parent[x];
        uf->parent[x] = root;
        x = next;
    }
    return root;
}

/*
 * Merge the sets containing a and b. Union by rank: hang the shorter tree
 * under the taller one so trees stay shallow; ranks only grow on a tie.
 */
bool uf_union(UnionFind *uf, int a, int b)
{
    int ra = uf_find(uf, a);
    int rb = uf_find(uf, b);
    if (ra == rb)
        return false;

    if (uf->rank[ra] < uf->rank[rb]) {
        uf->parent[ra] = rb;
    } else if (uf->rank[ra] > uf->rank[rb]) {
        uf->parent[rb] = ra;
    } else {
        uf->parent[rb] = ra;
        uf->rank[ra]++;
    }
    uf->sets--;
    return true;
}

bool uf_same(UnionFind *uf, int a, int b)
{
    return uf_find(uf, a) == uf_find(uf, b);
}
