/*
 * unionfind.h — Disjoint-set forest with path compression and union by rank.
 *
 * In the game the elements are rings (0..RING_COUNT, where RING_COUNT is the
 * core). Capturing gate r unions ring r with ring r+1, so "gate r is
 * captured" is exactly find(r) == find(r+1), and the player's captured
 * territory is the set containing ring 0.
 */
#ifndef UNIONFIND_H
#define UNIONFIND_H

#include <stdbool.h>

#define UF_MAX 64

typedef struct {
    int parent[UF_MAX]; /* parent[x] == x means x is a set's root */
    int rank[UF_MAX];   /* upper bound on the height of x's tree */
    int count;          /* number of elements */
    int sets;           /* number of disjoint sets */
} UnionFind;

void uf_init(UnionFind *uf, int count);
int  uf_find(UnionFind *uf, int x);
bool uf_union(UnionFind *uf, int a, int b); /* false if already joined */
bool uf_same(UnionFind *uf, int a, int b);

#endif /* UNIONFIND_H */
