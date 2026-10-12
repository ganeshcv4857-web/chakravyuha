/*
 * render.c — The battle screen: input, feedback and drawing of the
 * formation, the two warriors and the HUD. Game rules live in game.c.
 */
#include "app.h"
#include "online.h"
#include "portrait.h"
#include "sfx.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static const Game *G; /* game being drawn */

/*
 * Two battle layouts share this file:
 *   classic  the formation on the left, the side panel on the right (PCs,
 *            tablets);
 *   wide     phones held sideways: the canvas widens to the screen's shape,
 *            the formation sits in the middle and each warrior gets a column
 *            with a big card, big buttons and a big arrow pad.
 * board_x shifts the formation (drawn in its own coordinates) on screen.
 */
static bool  wide;
static float canvas_w = SCREEN_W, board_x = 0.0f;
static float col_w;                       /* wide: width of each side column */
static bool  pandava_left = true;         /* wide: which column is the Pandava's */
static float pad_reach = 56.0f, pad_radius = 28.0f, pad_hit = 36.0f;

static void layout_begin(const App *a)
{
    /* Two players: each column belongs to the player whose pad is in it. */
    pandava_left = a->mode != MODE_DUEL || a->p1_side == SIDE_PANDAVA;
    float aspect = (float)GetScreenWidth() / (float)(GetScreenHeight() > 0 ? GetScreenHeight() : 1);
    wide = ui_touch && aspect >= 1.6f;
    if (wide) {
        canvas_w = fminf(fmaxf(SCREEN_H * aspect, 1340.0f), 2200.0f);
        board_x  = (canvas_w - 820.0f) / 2.0f;
        col_w    = board_x;
        pad_reach = 86.0f, pad_radius = 46.0f, pad_hit = 58.0f;
    } else {
        canvas_w = SCREEN_W;
        board_x  = 0.0f;
        pad_reach = 56.0f, pad_radius = 28.0f, pad_hit = 36.0f;
    }
    ui_set_canvas_width(canvas_w);
}

/* The formation's camera: the canvas camera shifted by board_x. */
static Camera2D board_camera(Vector2 shake)
{
    Camera2D cam = ui_camera(shake);
    cam.offset.x += board_x * cam.zoom;
    return cam;
}

/* Where the formation sits on the canvas (its own coordinates + board_x). */
static float board_cx(void) { return board_x + G->cx; }

/* ------------------------------------------------------------------------ */
/* Geometry helpers                                                         */
/* ------------------------------------------------------------------------ */

static Vector2 node_pos(int id)
{
    return (Vector2){ G->graph.nodes[id].x, G->graph.nodes[id].y };
}

static float node_angle(int id)
{
    const Node *n = &G->graph.nodes[id];
    return atan2f(n->y - G->cy, n->x - G->cx);
}

static float wrap_angle(float a)
{
    while (a > PI)   a -= 2.0f * PI;
    while (a <= -PI) a += 2.0f * PI;
    return a;
}

static bool same_ring(int a, int b)
{
    const Node *na = &G->graph.nodes[a], *nb = &G->graph.nodes[b];
    return na->ring == nb->ring && na->ring < RING_COUNT;
}

/* Point a fraction t along edge a-b (an arc for ring edges). */
static Vector2 edge_point(int a, int b, float t)
{
    if (same_ring(a, b)) {
        float r  = G->graph.rings[G->graph.nodes[a].ring].radius;
        float a0 = node_angle(a);
        float an = a0 + wrap_angle(node_angle(b) - a0) * t;
        return (Vector2){ G->cx + r * cosf(an), G->cy + r * sinf(an) };
    }
    Vector2 pa = node_pos(a), pb = node_pos(b);
    return (Vector2){ pa.x + (pb.x - pa.x) * t, pa.y + (pb.y - pa.y) * t };
}

static void draw_edge(int a, int b, float thick, Color color)
{
    if (same_ring(a, b)) {
        float r  = G->graph.rings[G->graph.nodes[a].ring].radius;
        float a0 = node_angle(a) * RAD2DEG;
        float d  = wrap_angle(node_angle(b) - node_angle(a)) * RAD2DEG;
        DrawRing((Vector2){ G->cx, G->cy }, r - thick / 2, r + thick / 2, a0, a0 + d, 24,
                 color);
    } else {
        DrawLineEx(node_pos(a), node_pos(b), thick, color);
    }
}

static void draw_edge_partial(int a, int b, float t, float thick, Color color)
{
    DrawLineEx(node_pos(a), edge_point(a, b, t), thick, color);
}

static float ease_out(float t)
{
    float u = 1.0f - t;
    return 1.0f - u * u * u;
}

static Vector2 entity_pos(const Entity *e)
{
    if (e->move_t >= 1.0f || e->prev_node == e->node)
        return node_pos(e->node);
    return edge_point(e->prev_node, e->node, ease_out(e->move_t));
}

static bool is_gate_node(int id)
{
    for (int r = 0; r < RING_COUNT; r++)
        if (G->graph.gates[r].outer == id || G->graph.gates[r].inner == id)
            return true;
    return false;
}

static const char *node_label(int id)
{
    const Node *n = &G->graph.nodes[id];
    if (id == G->graph.core)
        return "CORE";
    return TextFormat("R%d.%d", n->ring + 1, n->index);
}

/* ------------------------------------------------------------------------ */
/* The battlefield                                                          */
/* ------------------------------------------------------------------------ */

void draw_battlefield_backdrop(Vector2 c, float radius, float t)
{
    DrawCircleGradient(c, radius + 70, C_GROUND, C_BG);

    /* Pebbles and hoof-scuffed dust: fixed pseudo-random speckles. */
    uint32_t s = 0x2C1B3C6Du;
    for (int i = 0; i < 320; i++) {
        s = s * 1664525u + 1013904223u;
        float a = (s >> 8) / 16777216.0f * 2.0f * PI;
        s = s * 1664525u + 1013904223u;
        float r = sqrtf((s >> 8) / 16777216.0f) * (radius + 40);
        Vector2 p = { c.x + cosf(a) * r, c.y + sinf(a) * r };
        bool dark = i % 3 != 0;
        DrawCircleV(p, dark ? 1.6f : 1.1f, dark ? Fade(C_BG, 0.35f) : Fade(C_IVORY, 0.10f));
    }

    /* Chariot-wheel spokes turning slowly under the formation. */
    for (int i = 0; i < 24; i++) {
        float a = t * 0.05f + i * (2.0f * PI / 24);
        Vector2 end = { c.x + cosf(a) * (radius + 30), c.y + sinf(a) * (radius + 30) };
        DrawLineEx(c, end, 1.0f, Fade(C_TRENCH, 0.35f));
    }

    /* A ring of spear-points marking the edge of the field. */
    for (int i = 0; i < 72; i++) {
        float a = i * (2.0f * PI / 72);
        Vector2 in  = { c.x + cosf(a) * (radius + 22), c.y + sinf(a) * (radius + 22) };
        Vector2 out = { c.x + cosf(a) * (radius + 34), c.y + sinf(a) * (radius + 34) };
        DrawLineEx(in, out, 2.0f, Fade(C_PANEL_ED, 0.55f));
    }
}

static void draw_rings(void)
{
    const Graph *g = &G->graph;
    for (int r = 0; r < RING_COUNT; r++)
        DrawRing((Vector2){ G->cx, G->cy }, g->rings[r].radius - 9, g->rings[r].radius + 9,
                 0, 360, 96, Fade(C_TRENCH, 0.6f));
    /* Every ring edge is drawn from the adjacency lists themselves. */
    for (int i = 0; i < g->node_count; i++)
        for (const Edge *e = g->nodes[i].adj; e; e = e->next)
            if (e->kind == EDGE_RING && i < e->to)
                draw_edge(i, e->to, 3.0f, C_RING);
}

static float rotation_progress(void)
{
    return 1.0f - G->rotation_anim / ROTATION_ANIM_TIME;
}

static bool gate_changed_recently(int ring)
{
    if (G->rotation_anim <= 0.0f)
        return false;
    for (int i = 0; i < G->change_count; i++)
        if (G->changes[i].ring == ring)
            return true;
    return false;
}

static void draw_gates(float t)
{
    float pulse = 0.5f + 0.5f * sinf(t * 4.0f);
    for (int r = 0; r < RING_COUNT; r++) {
        const Gate *gate = &G->graph.gates[r];
        if (gate_changed_recently(r))
            continue;
        Color c = gate->locked ? C_CAPTURED : C_GATE;
        draw_edge(gate->outer, gate->inner, 14.0f + 4.0f * pulse, Fade(c, 0.18f));
        draw_edge(gate->outer, gate->inner, 5.0f, c);
    }
    if (G->rotation_anim <= 0.0f)
        return;

    /*
     * Rotation animation:
     *   0.00-0.45  old gate edge flickers and burns out
     *   0.30-0.80  new gate edge grows from its outer node to its inner node
     *   0.80-1.00  new gate settles to its normal colour
     */
    float p = rotation_progress();
    for (int i = 0; i < G->change_count; i++) {
        const GateChange *ch = &G->changes[i];
        if (p < 0.45f) {
            float k = p / 0.45f;
            float flicker = (sinf(p * 90.0f) > 0.0f) ? 1.0f : 0.45f;
            draw_edge(ch->old_outer, ch->old_inner, 5.0f + 10.0f * k,
                      Fade(ColorLerp(C_IVORY, C_KAURAVA, k), (1.0f - k) * flicker));
            DrawCircleV(node_pos(ch->old_outer), 14.0f * (1.0f - k),
                        Fade(C_KAURAVA, 0.6f * (1.0f - k)));
        }
        if (p > 0.30f) {
            float k = fminf(1.0f, (p - 0.30f) / 0.50f);
            Color c = p < 0.8f ? C_IVORY : ColorLerp(C_IVORY, C_GATE, (p - 0.8f) / 0.2f);
            draw_edge_partial(ch->new_outer, ch->new_inner, ease_out(k), 18.0f, Fade(c, 0.25f));
            draw_edge_partial(ch->new_outer, ch->new_inner, ease_out(k), 5.0f, c);
            DrawCircleV(node_pos(ch->new_outer), 6.0f + 12.0f * (1.0f - k),
                        Fade(C_IVORY, 0.5f * (1.0f - k) + 0.2f));
        }
    }
    DrawRing((Vector2){ G->cx, G->cy }, p * FIELD_R * 1.1f - 3, p * FIELD_R * 1.1f + 3, 0,
             360, 96, Fade(C_ROTATE, 0.5f * (1.0f - p)));
}

static void draw_path(const Path *path, int from_pos, float thick, Color color)
{
    for (int i = from_pos; i + 1 < path->length; i++)
        draw_edge(path->nodes[i], path->nodes[i + 1], thick, color);
}

static void draw_overlays(const App *a, float t)
{
    if (a->view.show_astar && G->enemy.path.length > 0) {
        for (int i = 0; i < G->graph.node_count; i++)
            if (G->enemy.path.visited[i])
                DrawRing(node_pos(i), 10, 12.5f, 0, 360, 20, Fade(C_KAURAVA, 0.35f));
        draw_path(&G->enemy.path, G->chaser_ai ? G->enemy.path_pos : 0, 7.0f,
                  Fade(C_KAURAVA, 0.35f));
    }
    if (a->view.show_bfs && G->route.length > 1) {
        float pulse = 0.55f + 0.25f * sinf(t * 5.0f);
        draw_path(&G->route, 0, 9.0f, Fade(C_PANDAVA, pulse * 0.6f));
        for (int i = 1; i < G->route.length; i++) {
            Vector2 p = node_pos(G->route.nodes[i]);
            ui_text(TextFormat("%d", i), p.x + 9, p.y - 22, 15, Fade(C_PANDAVA, 0.95f));
        }
    }
}

static void draw_nodes(const App *a, float t)
{
    const Graph *g = &G->graph;
    for (int i = 0; i < g->node_count; i++) {
        Vector2 p = node_pos(i);
        if (i == g->core) {
            /* The sacred fire at the heart of the formation. */
            float pulse = 0.5f + 0.5f * sinf(t * 3.0f);
            DrawCircleV(p, 34 + 6 * pulse, Fade(C_CORE, 0.12f));
            DrawCircleV(p, 22, Fade(C_CORE, 0.35f));
            for (int f = 0; f < 7; f++) {
                float a0 = f * 2.0f * PI / 7 + t * 0.8f;
                float h = 18 + 5 * sinf(t * 7 + f * 1.7f);
                Vector2 tip = { p.x + cosf(a0) * h, p.y + sinf(a0) * h };
                DrawLineEx(p, tip, 5, Fade(C_CORE, 0.7f));
            }
            DrawPoly(p, 6, 12, t * 40.0f, C_CORE);
            DrawPoly(p, 6, 6, -t * 60.0f, C_IVORY);
            continue;
        }
        bool gate = is_gate_node(i);
        DrawCircleV(p, gate ? 9.0f : 7.0f, C_BG);
        DrawCircleV(p, gate ? 7.0f : 5.5f, gate ? C_GATE : C_NODE);
        if (a->view.show_labels)
            ui_text(node_label(i), p.x + 9, p.y + 5, 13, C_DIM);
    }
}

/* Key glyph: a letter, or an arrow head for the arrow-key player. */
static void key_badge(Vector2 m, int dir, KeySet keys, Color col)
{
    static const char *letters[] = { "A", "D", "W", "S" };
    Rectangle box = { m.x - 10, m.y - 10, 20, 20 };
    DrawRectangleRounded(box, 0.3f, 4, Fade(C_BG, 0.88f));
    DrawRectangleRoundedLinesEx(box, 0.3f, 4, 1.5f, Fade(col, 0.85f));
    if (keys != KEYS_ARROWS) {
        ui_text_center(letters[dir], m.x, m.y - 8, 16, col);
        return;
    }
    /* Arrow keys: left/right/up/down triangles. */
    Vector2 v[3];
    switch (dir) {
    case DIR_CCW: v[0] = (Vector2){ m.x - 5, m.y }; v[1] = (Vector2){ m.x + 4, m.y + 5 }; v[2] = (Vector2){ m.x + 4, m.y - 5 }; break;
    case DIR_CW:  v[0] = (Vector2){ m.x + 5, m.y }; v[1] = (Vector2){ m.x - 4, m.y - 5 }; v[2] = (Vector2){ m.x - 4, m.y + 5 }; break;
    case DIR_IN:  v[0] = (Vector2){ m.x, m.y - 5 }; v[1] = (Vector2){ m.x - 5, m.y + 4 }; v[2] = (Vector2){ m.x + 5, m.y + 4 }; break;
    default:      v[0] = (Vector2){ m.x, m.y + 5 }; v[1] = (Vector2){ m.x + 5, m.y - 4 }; v[2] = (Vector2){ m.x - 5, m.y - 4 }; break;
    }
    DrawTriangle(v[0], v[1], v[2], col);
}

/*
 * Show where a human warrior can step: key badges on a PC, glowing rings
 * (tap targets) on a touch screen.
 */
static void draw_move_hints_for(int node, KeySet keys, Color col, float t)
{
    for (int d = DIR_CCW; d <= DIR_OUT; d++) {
        int to = entity_neighbor(&G->graph, node, (MoveDir)d);
        if (to < 0)
            continue;
        if (ui_touch) {
            float pulse = 0.5f + 0.5f * sinf(t * 5.0f);
            DrawRing(node_pos(to), 12.0f, 15.5f + 2.0f * pulse, 0, 360, 28,
                     Fade(col, 0.55f + 0.35f * pulse));
        } else {
            key_badge(edge_point(node, to, 0.55f), d, keys, col);
        }
    }
}

/* Which sides are steered by a person on this device right now. */
static bool runner_is_human(const App *a)
{
    return a->pandava_human && !a->game.runner_ai && a->pandava_keys != KEYS_NONE;
}
static bool chaser_is_local(const App *a)
{
    return a->kaurava_human && a->kaurava_keys != KEYS_NONE;
}
static bool chaser_can_move(const App *a) { return chaser_is_local(a) && a->game.enemy_wake <= 0; }

static void draw_move_hints(const App *a, float t)
{
    if (G->state != STATE_PLAYING || a->view.intro > 0)
        return;
    if (runner_is_human(a))
        draw_move_hints_for(G->player.node, a->pandava_keys, C_PANDAVA, t);
    if (chaser_can_move(a))
        draw_move_hints_for(G->enemy.node, a->kaurava_keys, C_KAURAVA, t);
}

/* ------------------------------------------------------------------------ */
/* Touch arrow pads                                                         */
/* ------------------------------------------------------------------------ */

typedef struct {
    Vector2 c;    /* centre of the pad */
    int     side; /* 0 = runner (Pandava), 1 = chaser (Kaurava) */
} Pad;


static const Vector2 PAD_DIR[4] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } }; /* CCW CW IN OUT */

/* One pad per human side, in the empty corners below the formation. */
static int battle_pads(const App *a, Pad *out)
{
    int n = 0;
    if (!ui_touch)
        return 0;
    Vector2 left, right;
    if (wide) {
        /* At the bottom of each side column, under the thumbs. */
        float y = SCREEN_H - (pad_reach + pad_radius + 14);
        left  = (Vector2){ col_w / 2, y };
        right = (Vector2){ canvas_w - col_w / 2, y };
    } else {
        /* Tucked into the corners, clear of every room on the outer ring. */
        left  = (Vector2){ 86, 734 };
        right = (Vector2){ 734, 734 };
    }
    /* Solo on a phone: the only pad goes on the right, under the right thumb.
       (The command buttons then sit in the other column.) */
    bool solo = a->mode != MODE_DUEL;
    if (runner_is_human(a))
        out[n++] = (Pad){ (solo && wide) || a->pandava_keys == KEYS_ARROWS ? right : left, 0 };
    if (chaser_is_local(a))
        out[n++] = (Pad){ (solo && wide) || a->kaurava_keys == KEYS_ARROWS ? right : left, 1 };
    return n;
}

static Vector2 pad_button(const Pad *p, int dir)
{
    return (Vector2){ p->c.x + PAD_DIR[dir].x * pad_reach, p->c.y + PAD_DIR[dir].y * pad_reach };
}

static bool over_pad(const App *a, Vector2 m)
{
    Pad pads[2];
    int n = battle_pads(a, pads);
    for (int i = 0; i < n; i++)
        if (CheckCollisionPointCircle(m, pads[i].c, pad_reach + pad_hit))
            return true;
    return false;
}

/* Triangle pointing along one of the four pad directions. */
static void pad_arrow(Vector2 c, int dir, float s, Color col)
{
    Vector2 f = PAD_DIR[dir], n = { -f.y, f.x };
    Vector2 tip  = { c.x + f.x * s, c.y + f.y * s };
    Vector2 b1   = { c.x - f.x * s * 0.6f + n.x * s * 0.8f, c.y - f.y * s * 0.6f + n.y * s * 0.8f };
    Vector2 b2   = { c.x - f.x * s * 0.6f - n.x * s * 0.8f, c.y - f.y * s * 0.6f - n.y * s * 0.8f };
    /* raylib wants counter-clockwise order; pick the winding that is. */
    float cross = (b1.x - tip.x) * (b2.y - tip.y) - (b1.y - tip.y) * (b2.x - tip.x);
    if (cross > 0)
        DrawTriangle(tip, b2, b1, col);
    else
        DrawTriangle(tip, b1, b2, col);
}

/*
 * Circular arrow for "walk around the ring": clockwise or counter-clockwise,
 * which is what the move really does wherever the warrior stands.
 */
static void pad_rotate_icon(Vector2 c, bool clockwise, float r, Color col)
{
    const float a0 = 210.0f, a1 = 480.0f; /* a 270-degree arc, gap at upper right */
    DrawRing(c, r - 1.6f, r + 1.6f, a0, a1, 32, col);

    /* Arrowhead at the end the rotation runs toward. Angles grow clockwise on screen. */
    float end = (clockwise ? a1 : a0) * DEG2RAD;
    Vector2 radial = { cosf(end), sinf(end) };
    Vector2 along  = clockwise ? (Vector2){ -radial.y, radial.x } : (Vector2){ radial.y, -radial.x };
    Vector2 p   = { c.x + radial.x * r, c.y + radial.y * r };
    Vector2 tip = { p.x + along.x * 6, p.y + along.y * 6 };
    Vector2 b1  = { p.x + radial.x * 5 - along.x * 2, p.y + radial.y * 5 - along.y * 2 };
    Vector2 b2  = { p.x - radial.x * 5 - along.x * 2, p.y - radial.y * 5 - along.y * 2 };
    float cross = (b1.x - tip.x) * (b2.y - tip.y) - (b1.y - tip.y) * (b2.x - tip.x);
    if (cross > 0)
        DrawTriangle(tip, b2, b1, col);
    else
        DrawTriangle(tip, b1, b2, col);
}

static void draw_pads(const App *a)
{
    Pad pads[2];
    int n = battle_pads(a, pads);
    if (G->state != STATE_PLAYING)
        return;
    for (int i = 0; i < n; i++) {
        const Pad *p = &pads[i];
        bool runner = p->side == 0;
        const Character *who = runner ? &a->pandava : &a->kaurava;
        Color col = side_color(who->side);
        int node = runner ? G->player.node : G->enemy.node;
        bool awake = runner || G->enemy_wake <= 0;

        DrawCircleV(p->c, pad_reach + pad_radius + 8, Fade(C_BG, 0.55f));
        DrawRing(p->c, pad_reach + pad_radius + 6, pad_reach + pad_radius + 8, 0, 360, 48,
                 Fade(col, 0.6f));
        ui_text_center(a->mode == MODE_DUEL
                           ? TextFormat("P%d  %s", who->side == a->p1_side ? 1 : 2, who->name)
                           : who->name,
                       p->c.x, p->c.y - (pad_reach + pad_radius + (wide ? 36 : 28)),
                       wide ? 24 : 16, Fade(col, 0.95f));
        for (int d = 0; d < 4; d++) {
            Vector2 b = pad_button(p, d);
            bool can  = awake && entity_neighbor(&G->graph, node, (MoveDir)d) >= 0;
            bool held = a->view.pad_prev[p->side][d];
            DrawCircleV(b, pad_radius, held ? Fade(col, 0.55f) : Fade(C_PANEL, 0.92f));
            DrawRing(b, pad_radius - 2, pad_radius, 0, 360, 32, can ? col : Fade(C_DIM, 0.4f));
            Color ic = can ? C_IVORY : Fade(C_DIM, 0.45f);
            if (d == DIR_CCW || d == DIR_CW)
                pad_rotate_icon(b, d == DIR_CW, wide ? 18 : 11, ic); /* around the ring */
            else
                pad_arrow(b, d, wide ? 20 : 12, ic);                 /* through a gate */
        }
    }
}

static void draw_tokens(const App *a, float t)
{
    Vector2 pp = entity_pos(&G->player), ep = entity_pos(&G->enemy);
    bool asleep = G->enemy_wake > 0.0f && G->state == STATE_PLAYING;

    /* Whoever "wins" a shared node is drawn on top. */
    for (int pass = 0; pass < 2; pass++) {
        bool draw_runner = (pass == 0) == (G->state == STATE_LOST);
        if (draw_runner) {
            portrait_token(&a->pandava, pp, 19, 1.0f, t);
            if (a->view.blocked[0] > 0)
                DrawCircleV(pp, 23, Fade(C_KAURAVA, a->view.blocked[0] / 0.2f * 0.5f));
        } else {
            portrait_token(&a->kaurava, ep, 19, asleep ? 0.55f : 1.0f, t);
            if (a->view.blocked[1] > 0)
                DrawCircleV(ep, 23, Fade(C_KAURAVA, a->view.blocked[1] / 0.2f * 0.5f));
        }
    }
}

static void draw_banner_text(const char *text, Color c, float alpha, float y)
{
    float size = wide ? 44 : 34, w = ui_measure_title(text, size);
    DrawRectangleRounded((Rectangle){ board_cx() - w / 2 - 22, y - 8, w + 44, size + 18 }, 0.4f,
                         8, Fade(C_BG, 0.78f * alpha));
    ui_title(text, board_cx(), y, size, Fade(c, alpha));
}

static void draw_banners(const App *a)
{
    if (a->view.intro > 0) {
        float p = a->view.intro;
        float alpha = fminf(1.0f, p / 0.4f);
        draw_banner_text("THE CONCH SOUNDS", C_GOLD, alpha, 300);
        float fs = wide ? 28 : 22;
        ui_text_center(TextFormat("%s must breach the five rings.", a->pandava.name),
                       board_cx(), 364, fs, Fade(C_TEXT, alpha));
        ui_text_center(TextFormat("%s must stop them.", a->kaurava.name), board_cx(),
                       364 + fs * 1.3f, fs, Fade(C_TEXT, alpha));
        return;
    }
    if (G->rotation_anim > 0.0f) {
        float p = rotation_progress();
        float alpha = p < 0.15f ? p / 0.15f : (p > 0.75f ? (1.0f - p) / 0.25f : 1.0f);
        draw_banner_text("THE FORMATION SHIFTS", C_ROTATE, alpha, 18);
    }
}

/* ------------------------------------------------------------------------ */
/* End of battle                                                            */
/* ------------------------------------------------------------------------ */

static void end_buttons(App *a, float y, float alpha);

static void draw_end_screen(App *a, float t)
{
    if (G->state == STATE_PLAYING)
        return;
    float al = fminf(1.0f, a->view.end_timer / 0.6f);
    bool pandava_won = G->state == STATE_WON;
    const Character *winner = pandava_won ? &a->pandava : &a->kaurava;
    const Character *loser  = pandava_won ? &a->kaurava : &a->pandava;
    bool winner_human = pandava_won ? a->pandava_human : a->kaurava_human;
    float cx = board_cx();

    DrawRectangle(0, 0, wide ? (int)canvas_w : PANEL_X - 10, SCREEN_H, Fade(C_BG, 0.82f * al));

    /* Headline from the human's point of view. */
    const char *head;
    if (a->mode == MODE_DUEL)
        head = TextFormat("PLAYER %d TRIUMPHS",
                          (winner->side == a->p1_side) ? 1 : 2);
    else
        head = winner_human ? "VICTORY" : "DEFEAT";
    ui_title(head, cx, 40, 56, Fade(winner_human || a->mode == MODE_DUEL ? C_GOLD : C_KAURAVA, al));

    Rectangle card = { cx - 110, 118, 220, 250 };
    portrait_card(winner, card, t, true);
    ui_text_center(winner->name, cx, 378, 30, Fade(side_color(winner->side), al));

    const char *line = pandava_won
        ? TextFormat("%s broke the Chakravyuha and reached the sacred fire.", winner->name)
        : TextFormat("%s cut down %s. The formation holds.", winner->name, loser->name);
    ui_text_center(line, cx, 418, 20, Fade(C_TEXT, al));

    /* Battle record. */
    const char *labels[] = { "Time", "Moves", "Breaches", "Rotations", "A* searches" };
    float x0 = cx - 250;
    for (int i = 0; i < 5; i++) {
        float x = x0 + i * 125;
        ui_text_center(labels[i], x, 462, 16, Fade(C_DIM, al));
        const char *v = i == 0 ? TextFormat("%.1fs", G->elapsed)
                      : i == 1 ? TextFormat("%d", G->player.moves)
                      : i == 2 ? TextFormat("%d", G->breaches)
                      : i == 3 ? TextFormat("%d", G->rotations)
                               : TextFormat("%d", G->enemy.replans);
        ui_text_center(v, x, 484, 26, Fade(C_IVORY, al));
    }
    end_buttons(a, 560, al);
}

static void end_buttons(App *a, float y, float alpha)
{
    if (alpha < 0.95f)
        return;
    float cx = board_cx();
    float bw = wide ? 250 : 200, bh = wide ? 70 : 48, gap = wide ? 40 : 30;
    Rectangle rematch = { cx - bw * 1.5f - gap, y, bw, bh };
    Rectangle fresh   = { cx - bw * 0.5f, y, bw, bh };
    Rectangle menu    = { cx + bw * 0.5f + gap, y, bw, bh };
    int sel = a->sel % 3;
    if (a->mode == MODE_ONLINE && !a->is_host) {
        /* Only the host can start the next battle. */
        ui_text_center("Waiting for the host to start the next battle...", cx, y - 40,
                       wide ? 26 : 20, C_GOLD);
        if (ui_button(menu, ui_touch ? "Leave" : "Leave  (Esc)", true, NULL) ||
            IsKeyPressed(KEY_ENTER))
            online_leave(a);
        return;
    }
    if (a->mode == MODE_ONLINE) {
        if (ui_button(rematch, "Rematch", sel == 0, NULL) || (sel == 0 && IsKeyPressed(KEY_ENTER)))
            online_begin_battle(a, a->seed);
        else if (ui_button(fresh, "New field", sel == 1, NULL) ||
                 (sel == 1 && IsKeyPressed(KEY_ENTER)))
            online_begin_battle(a, 0);
        else if (ui_button(menu, "Leave", sel == 2, NULL) || (sel == 2 && IsKeyPressed(KEY_ENTER)))
            online_leave(a);
        return;
    }
    if (ui_button(rematch, ui_touch ? "Rematch" : "Rematch  (R)", sel == 0, NULL) ||
        (sel == 0 && IsKeyPressed(KEY_ENTER)))
        app_start_battle(a, a->seed);
    else if (ui_button(fresh, ui_touch ? "New field" : "New field  (N)", sel == 1, NULL) ||
             (sel == 1 && IsKeyPressed(KEY_ENTER)))
        app_start_battle(a, (uint32_t)time(NULL) ^ (uint32_t)(GetTime() * 1e6));
    else if (ui_button(menu, ui_touch ? "Main menu" : "Main menu  (Esc)", sel == 2, NULL) ||
             (sel == 2 && IsKeyPressed(KEY_ENTER)))
        app_set_screen(a, SCREEN_TITLE);
}

/* ------------------------------------------------------------------------ */
/* HUD                                                                      */
/* ------------------------------------------------------------------------ */

static float hud_heading(const char *text, float y)
{
    ui_text(text, PANEL_X + 22, y, 15, C_GOLD);
    DrawLineEx((Vector2){ PANEL_X + 22, y + 21 }, (Vector2){ PANEL_X + PANEL_W - 22, y + 21 },
               1.0f, C_PANEL_ED);
    return y + 28;
}

static float hud_row(const char *label, const char *value, Color vc, float y)
{
    ui_text(label, PANEL_X + 22, y, 17, C_TEXT);
    ui_text(value, PANEL_X + 238, y, 17, vc);
    return y + 23;
}

static Color log_color(LogKind k)
{
    switch (k) {
    case LOGK_PLAYER:  return C_PANDAVA;
    case LOGK_ENEMY:   return Fade(C_KAURAVA, 0.95f);
    case LOGK_ROTATE:  return C_ROTATE;
    case LOGK_CAPTURE: return C_CAPTURED;
    case LOGK_WIN:     return C_CORE;
    case LOGK_LOSE:    return C_KAURAVA;
    default:           return C_DIM;
    }
}

static void hud_card(const App *a, const Character *ch, float x, float glow, float t)
{
    Rectangle r = { x, 22, 200, 142 };
    portrait_card(ch, r, t, glow > 0);
    ui_text_center(ch->name, x + 100, 170, 22, side_color(ch->side));
    ui_text_center(app_controller_label(a, ch->side), x + 100, 196, 14, C_DIM);
}

/* Phone layout: a column per warrior, big type, no chronicle. */
static void draw_wide_hud(const App *a, float t)
{
    for (int side = 0; side < 2; side++) {
        const Character *ch = side == 0 ? &a->pandava : &a->kaurava;
        bool left = (side == 0) == pandava_left;
        float x0 = left ? 0 : canvas_w - col_w;
        float cw = fminf(col_w - 40, 260), ch_h = cw * 0.66f;
        Rectangle card = { x0 + (col_w - cw) / 2, 16, cw, ch_h };
        portrait_card(ch, card, t, a->view.glow[side] > 0);
        ui_text_center(ch->name, x0 + col_w / 2, card.y + ch_h + 6, 30, side_color(ch->side));
    }

    /* Left column: the Pandava's progress, in big type. */
    float cw = fminf(col_w - 40, 260);
    float y = 16 + cw * 0.66f + 52;
    float x = (pandava_left ? 0 : canvas_w - col_w) + (col_w - cw) / 2;
    int ring = G->graph.nodes[G->player.node].ring;
    ui_text(ring == RING_COUNT ? "At the sacred fire!" : TextFormat("Ring %d of %d", ring + 1, RING_COUNT),
            x, y, 28, C_PANDAVA);
    ui_text(TextFormat("Gates breached  %d / %d", G->breaches, RING_COUNT), x, y + 38, 24, C_TEXT);
    ui_text("Shift in", x, y + 74, 24, C_ROTATE);
    for (int i = 0; i < G->rotation_k; i++) {
        Rectangle pip = { x + 96 + i * 26, y + 79, 18, 18 };
        if (i < G->since_rotation)
            DrawRectangleRec(pip, C_ROTATE);
        else
            DrawRectangleLinesEx(pip, 2.0f, C_ROTATE);
    }
    if (G->enemy_wake > 0.0f && G->state == STATE_PLAYING)
        ui_text(TextFormat("%s readies...", a->kaurava.name), x, y + 110, 22, C_KAURAVA);
}

static void draw_hud(const App *a, float t)
{
    if (wide) {
        draw_wide_hud(a, t);
        return;
    }
    ui_panel((Rectangle){ PANEL_X, 10, PANEL_W, SCREEN_H - 20 }, C_PANEL_ED);

    /* The two warriors, face to face. */
    hud_card(a, &a->pandava, PANEL_X + 22, a->view.glow[0], t);
    hud_card(a, &a->kaurava, PANEL_X + PANEL_W - 222, a->view.glow[1], t);
    Vector2 vs = { PANEL_X + PANEL_W / 2.0f, 93 };
    DrawCircleV(vs, 19, C_PANEL_ED);
    DrawCircleV(vs, 16, C_BG);
    ui_text_center("VS", vs.x, vs.y - 10, 19, C_GOLD);

    float y = hud_heading("THE BATTLE", 220);
    const Graph *g = &G->graph;
    int ring = g->nodes[G->player.node].ring;
    y = hud_row(TextFormat("%s at", a->pandava.name),
                ring == RING_COUNT ? "the CORE"
                                   : TextFormat("ring %d of %d  (%s)", ring + 1, RING_COUNT,
                                                node_label(G->player.node)),
                C_PANDAVA, y);
    y = hud_row("Gates breached", TextFormat("%d", G->breaches), C_TEXT, y);

    ui_text("Formation shifts in", PANEL_X + 22, y, 17, C_TEXT);
    for (int i = 0; i < G->rotation_k; i++) {
        Rectangle pip = { PANEL_X + 238 + i * 19, y + 2, 13, 13 };
        if (i < G->since_rotation)
            DrawRectangleRec(pip, C_ROTATE);
        else
            DrawRectangleLinesEx(pip, 1.5f, C_ROTATE);
    }
    ui_text(TextFormat("%d  (K=%d)", game_rotation_countdown(G), G->rotation_k),
            PANEL_X + 246 + G->rotation_k * 19, y, 17, C_ROTATE);
    y += 23;
    y = hud_row("Rotations / crossings", TextFormat("%d / %d", G->rotations, G->crossings),
                C_TEXT, y);
    y = hud_row("BFS route to core",
                G->route.length ? TextFormat("%d hops", G->route.length - 1) : "none",
                C_PANDAVA, y);
    y = hud_row(TextFormat("%s speed", a->kaurava.name),
                G->enemy_wake > 0.0f && G->state == STATE_PLAYING
                    ? "readying..."
                    : TextFormat("%.1f steps/s", 1.0f / G->enemy_step_interval),
                C_KAURAVA, y);
    y = hud_row(G->chaser_ai ? "A* hunt route" : "A* scout route",
                G->enemy.path.length
                    ? TextFormat("%d hops, %d expanded", G->enemy.path.length - 1,
                                 G->enemy.path.expanded)
                    : "none",
                C_KAURAVA, y);

    char territory[64] = "";
    for (int r = 0; r <= RING_COUNT; r++)
        if (uf_same((UnionFind *)&G->territory, 0, r))
            strcat(territory, r == RING_COUNT ? "C" : TextFormat("%d ", r + 1));
    y = hud_row("Gate capture",
                G->capture_mode ? TextFormat("ON  {%s}", territory) : "off",
                G->capture_mode ? C_CAPTURED : C_DIM, y);

    /* Chronicle of the battle. */
    float controls_y = SCREEN_H - (ui_touch ? 146 : (a->view.show_help ? 128 : 58));
    y = hud_heading("CHRONICLE", y + 6);
    for (int i = 0; y + 18 < controls_y - 4; i++) {
        const LogLine *line = game_log_line(G, i);
        if (!line)
            break;
        char text[LOG_TEXT];
        snprintf(text, sizeof text, "%s", line->text);
        while (ui_measure(text, 14) > PANEL_W - 44 && strlen(text) > 4)
            text[strlen(text) - 1] = '\0';
        ui_text(text, PANEL_X + 22, y, 14, Fade(log_color(line->kind), 1.0f - i * 0.06f));
        y += 18;
    }

    if (ui_touch) {
        hud_heading("COMMANDS", controls_y);
        return; /* the command buttons are drawn in this space */
    }
    y = hud_heading("CONTROLS  (H)", controls_y);
    if (!a->view.show_help)
        return;
    if (a->mode == MODE_DUEL)
        ui_text(TextFormat("P1 %s: WASD      P2 %s: Arrow keys",
                           side_name(a->p1_side),
                           side_name(a->p1_side == SIDE_PANDAVA ? SIDE_KAURAVA : SIDE_PANDAVA)),
                PANEL_X + 22, y, 15, C_GATE);
    else
        ui_text("Move: WASD or Arrows   (W / Up = through a gate)", PANEL_X + 22, y, 15,
                C_GATE);
    ui_text("B route   V A*   L labels   C capture   [ ] K   G rotate", PANEL_X + 22, y + 21,
            15, C_TEXT);
    ui_text(a->mode == MODE_SOLO && a->pandava_human
                ? "P autopilot   M mute   R rematch   Esc menu"
                : "M mute   R rematch   N new field   Esc menu",
            PANEL_X + 22, y + 42, 15, C_TEXT);
}

/* ------------------------------------------------------------------------ */
/* Command buttons: toggles that work with keys, clicks and taps            */
/* ------------------------------------------------------------------------ */

typedef enum {
    CMD_ROUTE, CMD_ASTAR, CMD_LABELS, CMD_CAPTURE, CMD_AUTOPILOT, CMD_MUTE, CMD_MENU, CMD_COUNT
} Command;

static bool command_available(const App *a, Command c)
{
    if (c == CMD_AUTOPILOT)
        return a->mode == MODE_SOLO && a->pandava_human;
    if (c == CMD_MENU)
        return ui_touch; /* keyboards have Esc */
    if (c == CMD_CAPTURE)
        return a->mode != MODE_ONLINE; /* agreed before an online battle */
    return true;
}

static bool command_on(const App *a, Command c)
{
    switch (c) {
    case CMD_ROUTE:     return a->view.show_bfs;
    case CMD_ASTAR:     return a->view.show_astar;
    case CMD_LABELS:    return a->view.show_labels;
    case CMD_CAPTURE:   return a->game.capture_mode;
    case CMD_AUTOPILOT: return a->game.runner_ai && a->pandava_human;
    case CMD_MUTE:      return sfx_muted();
    default:            return false;
    }
}

static const char *command_label(Command c)
{
    static const char *keys[CMD_COUNT]  = { "B route", "V A*", "L labels", "C capture",
                                            "P autopilot", "M mute", "Menu" };
    static const char *touch[CMD_COUNT] = { "Route", "A* path", "Labels", "Capture",
                                            "Autopilot", "Mute", "Menu" };
    return ui_touch ? touch[c] : keys[c];
}

/*
 * Where each available command sits: a slim row under the formation on a
 * PC, finger-sized buttons at the bottom of the side panel on a touch screen.
 */
static Rectangle command_rect(const App *a, Command c)
{
    int slot = 0;
    for (int i = 0; i < (int)c; i++)
        if (command_available(a, (Command)i))
            slot++;
    if (wide) {
        float cw = fminf(col_w - 40, 300), bw = (cw - 12) / 2, bh = 58;
        float x0 = (pandava_left ? canvas_w - col_w : 0) + (col_w - cw) / 2;
        float y0 = 16 + fminf(col_w - 40, 260) * 0.66f + 56;
        return (Rectangle){ x0 + (slot % 2) * (bw + 12), y0 + (slot / 2) * (bh + 10), bw, bh };
    }
    if (ui_touch) {
        float w = (PANEL_W - 44 - 3 * 8) / 4.0f;
        return (Rectangle){ PANEL_X + 22 + (slot % 4) * (w + 8), SCREEN_H - 112 + (slot / 4) * 50.0f,
                            w, 42 };
    }
    float x = 20;
    for (int i = 0; i < (int)c; i++)
        if (command_available(a, (Command)i))
            x += ui_measure(command_label((Command)i), 14) + 26;
    return (Rectangle){ x, SCREEN_H - 36, ui_measure(command_label(c), 14) + 18, 24 };
}

static void run_command(App *a, Command c)
{
    Game *g = &a->game;
    switch (c) {
    case CMD_ROUTE:  a->view.show_bfs    = !a->view.show_bfs; break;
    case CMD_ASTAR:  a->view.show_astar  = !a->view.show_astar; break;
    case CMD_LABELS: a->view.show_labels = !a->view.show_labels; break;
    case CMD_MUTE:   sfx_toggle_mute(); break;
    case CMD_CAPTURE:
        game_toggle_capture(g);
        a->capture = g->capture_mode;
        break;
    case CMD_AUTOPILOT:
        a->autopilot = !a->autopilot;
        game_set_runner_ai(g, a->autopilot);
        break;
    case CMD_MENU:
        if (a->mode == MODE_ONLINE)
            online_leave(a);
        else
            app_set_screen(a, SCREEN_TITLE);
        break;
    default:
        break;
    }
}

static void draw_commands(const App *a)
{
    for (int i = 0; i < CMD_COUNT; i++) {
        Command c = (Command)i;
        if (!command_available(a, c))
            continue;
        Rectangle r = command_rect(a, c);
        bool on = command_on(a, c);
        float size = wide ? 24 : (ui_touch ? 18 : 14);
        DrawRectangleRounded(r, 0.4f, 6, on ? Fade(C_GATE, 0.28f) : Fade(C_PANEL_HI, 0.9f));
        if (ui_touch)
            DrawRectangleRoundedLinesEx(r, 0.4f, 6, 1.5f, on ? C_GOLD : C_PANEL_ED);
        ui_text_center(command_label(c), r.x + r.width / 2, r.y + (r.height - size) / 2 - 1,
                       size, on ? C_GOLD : (ui_touch ? C_TEXT : C_DIM));
    }
}

void battle_draw(App *a, float t)
{
    G = &a->game;
    layout_begin(a);
    Vector2 shake = { 0, 0 };
    if (a->view.shake > 0.0f) {
        float s = a->view.shake * 14.0f;
        shake = (Vector2){ sinf(t * 83.0f) * s, cosf(t * 71.0f) * s };
    }

    ui_begin(board_camera(shake));
    draw_battlefield_backdrop((Vector2){ G->cx, G->cy }, FIELD_R, t);
    draw_rings();
    draw_overlays(a, t);
    draw_gates(t);
    draw_nodes(a, t);
    draw_move_hints(a, t);
    draw_tokens(a, t);
    ui_end();

    ui_begin(ui_camera((Vector2){ 0, 0 }));
    draw_banners(a);
    draw_pads(a);
    draw_hud(a, t);
    draw_commands(a);
    draw_end_screen(a, t);
    ui_end();
}

/* ------------------------------------------------------------------------ */
/* Input and feedback                                                       */
/* ------------------------------------------------------------------------ */

static int read_dir(KeySet ks, bool (*key)(int))
{
    if (ks == KEYS_NONE)
        return -1;
    bool wasd = ks != KEYS_ARROWS, arrows = ks != KEYS_WASD;
    if ((wasd && key(KEY_A)) || (arrows && key(KEY_LEFT)))  return DIR_CCW;
    if ((wasd && key(KEY_D)) || (arrows && key(KEY_RIGHT))) return DIR_CW;
    if ((wasd && key(KEY_W)) || (arrows && key(KEY_UP)))    return DIR_IN;
    if ((wasd && key(KEY_S)) || (arrows && key(KEY_DOWN)))  return DIR_OUT;
    return -1;
}

/*
 * Drive one human side. A fresh key press is buffered briefly so quick taps
 * during the cooldown are not lost; holding a key keeps walking.
 */
static void human_side(App *a, int side, KeySet keys, float dt, int touch_pressed,
                       int touch_held)
{
    Game *g = &a->game;
    InputQueue *q = &a->view.input[side];
    Entity *e = side == 0 ? &g->player : &g->enemy;
    bool (*try_move)(Game *, MoveDir) = side == 0 ? game_try_move : game_try_move_chaser;
    bool ready = e->cooldown <= 0.0f && (side == 0 || g->enemy_wake <= 0.0f);

    int pressed = read_dir(keys, IsKeyPressed);
    if (pressed < 0)
        pressed = touch_pressed;
    if (pressed >= 0) {
        q->dir   = pressed;
        q->timer = 0.35f;
    }
    if (q->dir >= 0) {
        q->timer -= dt;
        if (ready) {
            if (!try_move(g, (MoveDir)q->dir))
                a->view.blocked[side] = 0.2f;
            q->dir = -1;
        } else if (q->timer <= 0.0f) {
            q->dir = -1;
        }
    } else {
        int held = read_dir(keys, IsKeyDown);
        if (held < 0)
            held = touch_held;
        if (held >= 0 && ready && entity_neighbor(&g->graph, e->node, (MoveDir)held) >= 0)
            try_move(g, (MoveDir)held);
    }
}

/*
 * Touch input for one frame, per side (0 runner, 1 chaser): which pad
 * arrow was just pressed or is held, or which glowing node was tapped.
 */
static void read_touch(App *a, int pressed[2], int held[2])
{
    pressed[0] = pressed[1] = held[0] = held[1] = -1;
    if (!ui_touch)
        return;

    Vector2 pts[8];
    int n = ui_pointers(pts, 8);
    Pad pads[2];
    int np = battle_pads(a, pads);
    for (int i = 0; i < np; i++) {
        const Pad *p = &pads[i];
        for (int d = 0; d < 4; d++) {
            bool now = false;
            for (int k = 0; k < n; k++)
                if (CheckCollisionPointCircle(pts[k], pad_button(p, d), pad_hit))
                    now = true;
            if (now) {
                held[p->side] = d;
                if (!a->view.pad_prev[p->side][d])
                    pressed[p->side] = d;
            }
            a->view.pad_prev[p->side][d] = now;
        }
    }

    /* A tap on a glowing node next to a human warrior moves that warrior. */
    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        return;
    Vector2 m = ui_mouse();
    if (over_pad(a, m))
        return;
    m.x -= board_x; /* into the formation's own coordinates */
    if (m.x < 0 || m.x > 820)
        return;
    int best_side = -1, best_dir = -1;
    float best = 42.0f; /* how far from a node a tap may land */
    for (int side = 0; side < 2; side++) {
        if ((side == 0 && !runner_is_human(a)) || (side == 1 && !chaser_can_move(a)))
            continue;
        int node = side == 0 ? G->player.node : G->enemy.node;
        for (int d = 0; d < 4; d++) {
            int to = entity_neighbor(&G->graph, node, (MoveDir)d);
            if (to < 0)
                continue;
            Vector2 q = node_pos(to);
            float dist = sqrtf((q.x - m.x) * (q.x - m.x) + (q.y - m.y) * (q.y - m.y));
            if (dist < best) {
                best = dist;
                best_side = side;
                best_dir = d;
            }
        }
    }
    if (best_side >= 0 && pressed[best_side] < 0)
        pressed[best_side] = best_dir;
}

static void play_feedback(App *a, float dt)
{
    Game *g = &a->game;
    unsigned ev = g->events;
    g->events = 0;

    if (ev & EV_WIN) {
        sfx_play(SFX_CONCH);
        sfx_play(SFX_VICTORY);
        sfx_music(MUSIC_MENU);
    } else if (ev & EV_LOSE) {
        sfx_play(SFX_CLASH_HEAVY);
        sfx_play(SFX_DEFEAT);
        sfx_music(MUSIC_MENU);
    } else if (ev & EV_ROTATE) {
        sfx_play(SFX_DRUM_ROLL);
    } else if (ev & EV_CAPTURE) {
        sfx_play(SFX_CAPTURE);
    } else if (ev & EV_BREACH) {
        sfx_play_ex(SFX_CLASH, 1.0f, 0.95f + 0.1f * (g->breaches % 3), 0.0f);
    } else if (ev & EV_ENEMY_GATE) {
        sfx_play(SFX_ENEMY_GATE);
    } else if (ev & EV_BLOCKED) {
        sfx_play(SFX_BLOCKED);
    }

    if (ev & EV_STEP) {
        sfx_play(SFX_STEP);
        a->view.glow[0] = 0.35f;
    }
    if (ev & EV_ENEMY_STEP) {
        /* Louder and panned toward the chaser as it closes in. */
        int dist[MAX_NODES];
        bfs_distances(&g->graph, g->player.node, dist);
        int d = dist[g->enemy.node] < 0 ? 10 : dist[g->enemy.node];
        float vol = fmaxf(0.25f, fminf(1.0f, 1.2f - d / 8.0f));
        float pan = (g->graph.nodes[g->enemy.node].x - FIELD_CX) / FIELD_R * 0.6f;
        sfx_play_ex(SFX_ENEMY_STEP, vol, 1.0f, pan);
        a->view.glow[1] = 0.35f;
    }

    if (ev & EV_ROTATE)
        a->view.shake = 0.35f;
    if (ev & EV_LOSE)
        a->view.shake = 0.55f;
    sfx_intensity(g->rotations / 4.0f);

    /* Heartbeat drum when the chaser is two steps away from a human runner. */
    a->view.danger_cooldown -= dt;
    if (a->pandava_human && g->state == STATE_PLAYING && g->enemy_wake <= 0 &&
        a->view.danger_cooldown <= 0) {
        int dist[MAX_NODES];
        bfs_distances(&g->graph, g->enemy.node, dist);
        if (dist[g->player.node] >= 0 && dist[g->player.node] <= 2) {
            sfx_play(SFX_DANGER);
            a->view.danger_cooldown = 1.1f;
        }
    }
}

void battle_update(App *a, float dt)
{
    Game *g = &a->game;
    View *v = &a->view;
    G = g;
    layout_begin(a);

    /* Keyboard shortcuts for the commands. */
    const struct { int key; Command cmd; } shortcuts[] = {
        { KEY_B, CMD_ROUTE }, { KEY_V, CMD_ASTAR }, { KEY_L, CMD_LABELS },
        { KEY_C, CMD_CAPTURE }, { KEY_P, CMD_AUTOPILOT }, { KEY_M, CMD_MUTE },
    };
    for (int i = 0; i < 6; i++)
        if (IsKeyPressed(shortcuts[i].key) && command_available(a, shortcuts[i].cmd))
            run_command(a, shortcuts[i].cmd);

    /* The same commands by click or tap. */
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        Vector2 m = ui_mouse();
        for (int i = 0; i < CMD_COUNT; i++) {
            if (command_available(a, (Command)i) &&
                CheckCollisionPointRec(m, command_rect(a, (Command)i))) {
                run_command(a, (Command)i);
                if (a->screen != SCREEN_BATTLE)
                    return;
            }
        }
    }

    bool online = a->mode == MODE_ONLINE;
    if (IsKeyPressed(KEY_H)) v->show_help = !v->show_help;
    if (!online) { /* these would change one copy of an online battle */
        if (IsKeyPressed(KEY_LEFT_BRACKET) || IsKeyPressed(KEY_MINUS))
            game_set_rotation_k(g, g->rotation_k - 1);
        if (IsKeyPressed(KEY_RIGHT_BRACKET) || IsKeyPressed(KEY_EQUAL))
            game_set_rotation_k(g, g->rotation_k + 1);
        if (IsKeyPressed(KEY_G) && g->state == STATE_PLAYING)
            game_rotate(g); /* force a rotation, for demos */
        a->rotation_k = g->rotation_k;
    }
    if (ui_back_pressed()) {
        if (online)
            online_leave(a);
        else
            app_set_screen(a, SCREEN_TITLE);
        return;
    }
    if (IsKeyPressed(KEY_R) && (!online || a->is_host)) {
        if (online)
            online_begin_battle(a, a->seed);
        else
            app_start_battle(a, a->seed);
        return;
    }
    if (IsKeyPressed(KEY_N) && (!online || a->is_host)) {
        if (online)
            online_begin_battle(a, 0);
        else
            app_start_battle(a, (uint32_t)time(NULL) ^ (uint32_t)(GetTime() * 1e6));
        return;
    }

    for (int i = 0; i < 2; i++) {
        if (v->blocked[i] > 0) v->blocked[i] -= dt;
        if (v->glow[i] > 0)    v->glow[i]    -= dt;
    }
    if (v->shake > 0) v->shake -= dt;

    if (g->state != STATE_PLAYING) {
        v->end_timer += dt;
        /* Keyboard focus for the end buttons. */
        if (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A)) a->sel = (a->sel + 2) % 3;
        if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D)) a->sel = (a->sel + 1) % 3;
        game_update(g, dt); /* finish slide animations */
        play_feedback(a, dt);
        return;
    }

    if (v->intro > 0) { /* the conch is still sounding */
        v->intro -= dt;
        return;
    }

    int touch_pressed[2], touch_held[2];
    read_touch(a, touch_pressed, touch_held);

    if (online && !a->is_host) {
        /* Guest: send what the player presses; the host decides the moves,
           which arrive as MV messages (online.c). */
        int side = a->p1_side == SIDE_PANDAVA ? 0 : 1;
        KeySet keys = side == 0 ? a->pandava_keys : a->kaurava_keys;
        int pressed = read_dir(keys, IsKeyPressed);
        if (pressed < 0)
            pressed = touch_pressed[side];
        int held = read_dir(keys, IsKeyDown);
        if (held < 0)
            held = touch_held[side];
        a->net_repeat -= dt;
        if (pressed >= 0 || (held >= 0 && a->net_repeat <= 0.0f)) {
            online_send_input(pressed >= 0 ? pressed : held);
            a->net_repeat = 0.12f;
        }
        game_update(g, dt); /* timers and animations only */
        play_feedback(a, dt);
        return;
    }

    int runner_before = g->player.node, chaser_before = g->enemy.node;
    if (runner_is_human(a))
        human_side(a, 0, a->pandava_keys, dt, touch_pressed[0], touch_held[0]);
    else if (online && a->pandava_keys == KEYS_NONE)
        human_side(a, 0, KEYS_NONE, dt, a->net_dir, -1); /* the guest's move */
    if (chaser_is_local(a))
        human_side(a, 1, a->kaurava_keys, dt, touch_pressed[1], touch_held[1]);
    else if (online && a->kaurava_keys == KEYS_NONE)
        human_side(a, 1, KEYS_NONE, dt, a->net_dir, -1);
    a->net_dir = -1;

    game_update(g, dt);
    if (online)
        online_broadcast_moves(a, runner_before, chaser_before);
    play_feedback(a, dt);
    if (g->state != STATE_PLAYING)
        a->sel = 0;
}
