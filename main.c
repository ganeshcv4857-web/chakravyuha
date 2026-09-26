/*
 * main.c — Chakravyuha: window, screen flow and the main loop.
 *
 * Game rules live in game.c, graph algorithms in graph.c / pathfind.c,
 * the battle screen in render.c and the menus in menu.c.
 *
 * Command line:
 *   --seed N      formation seed for the first battle (default: time-based)
 *   --k N         rotation threshold K (default 3)
 *   --capture     start with gate capture (Union-Find) switched on
 *   --shots       scripted tour of every screen that saves screenshots
 *   --shots-duel  the same for a two-player game
 *   --touch       phone-style touch controls (arrow pads, tap to move)
 *   --size WxH    window size, e.g. --touch --size 1600x720 to try a phone layout
 */
#include "app.h"
#include "net.h"
#include "online.h"
#include "portrait.h"
#include "sfx.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(__ANDROID__)
#include <android/native_activity.h>
#include <android/window.h>
#include <android_native_app_glue.h>
struct android_app *GetAndroidApp(void); /* provided by raylib's Android layer */
#endif

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
/* Small bridges to the web page (web/shell.html). */
EM_JS(int, web_is_touch, (void), {
    return (('ontouchstart' in window) || navigator.maxTouchPoints > 0) ? 1 : 0;
});
EM_JS(int, web_width, (void), { return window.innerWidth; });
EM_JS(int, web_height, (void), { return window.innerHeight; });
EM_JS(void, web_ready, (void), { if (window.onGameReady) window.onGameReady(); });
EM_JS(void, web_set_root, (int at_root), { window.chakraAtRoot = at_root; });
/* Page URL parameter (e.g. ?test=host), copied into out; "" if absent. */
EM_JS(void, web_param, (const char *name, char *out, int size), {
    var v = new URLSearchParams(location.search).get(UTF8ToString(name)) || "";
    stringToUTF8(v, out, size);
});

/*
 * Online self-test, used to check the network play end to end in two browser
 * tabs: ?test=host in one, ?test=join&code=ABCD in the other. Each side plays
 * scripted moves and logs the result; the two logs must agree.
 */
static struct { int role; float timer; bool done; } webtest; /* role 1 host, 2 guest */

static int dir_towards(const Graph *g, int from, int to)
{
    for (int d = DIR_CCW; d <= DIR_OUT; d++)
        if (entity_neighbor(g, from, (MoveDir)d) == to)
            return d;
    return -1;
}

static void webtest_step(App *a, float dt)
{
    if (!webtest.role || webtest.done)
        return;
    webtest.timer -= dt;
    if (a->screen == SCREEN_TITLE && webtest.timer < 0.0f) {
        a->mode = MODE_ONLINE;
        a->is_host = webtest.role == 1;
        if (a->is_host) {
            a->p1_side = SIDE_PANDAVA;
            a->pandava_human = a->kaurava_human = true;
            a->pandava_keys = KEYS_ANY, a->kaurava_keys = KEYS_NONE;
            character_from_preset(&a->pandava, SIDE_PANDAVA, 0);
            online_after_forge(a); /* opens the room */
        } else {
            web_param("code", a->code_input, sizeof a->code_input);
            app_set_screen(a, SCREEN_ONLINE);
            a->online_phase = ONLINE_CODE;
            /* same path as pressing Join */
            net_connect();
            a->online_phase = ONLINE_JOINING;
        }
        webtest.timer = 1e9f;
    }
    if (a->screen == SCREEN_ONLINE && a->room[0] && a->is_host) {
        static bool logged;
        if (!logged)
            printf("TEST: room %s\n", a->room), logged = true;
    }
    if (a->screen == SCREEN_CREATE && !a->is_host && a->screen_time > 0.2f) {
        character_from_preset(app_character(a, a->p1_side), a->p1_side, 2); /* Drona */
        online_after_forge(a);
    }
    if (a->screen == SCREEN_VERSUS && a->is_host && a->screen_time > 0.5f)
        online_begin_battle(a, 424242);
    if (a->screen == SCREEN_BATTLE && a->view.intro <= 0 && a->game.state == STATE_PLAYING) {
        Game *g = &a->game;
        if (a->is_host && g->player.cooldown <= 0 && g->route.length > 1) {
            int d = dir_towards(&g->graph, g->player.node, g->route.nodes[1]);
            if (d >= 0 && g->route.nodes[1] != g->enemy.node) {
                a->view.input[0].dir = d;
                a->view.input[0].timer = 0.35f;
            }
        }
        if (!a->is_host && g->enemy.path.length > 1 && webtest.timer <= 0.0f) {
            int d = dir_towards(&g->graph, g->enemy.node, g->enemy.path.nodes[1]);
            if (d >= 0)
                online_send_input(d);
            webtest.timer = 0.2f;
        }
        if (webtest.timer > 1e8f)
            webtest.timer = 0.0f;
    }
    if (a->screen == SCREEN_BATTLE && a->game.state != STATE_PLAYING) {
        printf("TEST: end state=%d runner=%d chaser=%d rotations=%d crossings=%d moves=%d\n",
               a->game.state, a->game.player.node, a->game.enemy.node, a->game.rotations,
               a->game.crossings, a->game.player.moves);
        webtest.done = true;
    }
}
#endif

static App      app;
static uint32_t cli_seed;
static bool     cli_seed_pending;

/* ------------------------------------------------------------------------ */
/* App helpers used by the screens                                          */
/* ------------------------------------------------------------------------ */

int app_human_count(const App *a)
{
    return a->mode == MODE_DUEL ? 2 : 1; /* online: only the local warrior */
}

Side app_human_side(const App *a, int i)
{
    if (i == 0)
        return a->p1_side;
    return a->p1_side == SIDE_PANDAVA ? SIDE_KAURAVA : SIDE_PANDAVA;
}

Character *app_character(App *a, Side s)
{
    return s == SIDE_PANDAVA ? &a->pandava : &a->kaurava;
}

const char *app_controller_label(const App *a, Side s)
{
    bool human = s == SIDE_PANDAVA ? a->pandava_human : a->kaurava_human;
    if (a->mode == MODE_ONLINE)
        return s == a->p1_side ? (ui_touch ? "You  -  tap or use the arrow pad" : "You  -  WASD or Arrow keys")
                               : "Online opponent";
    if (!human)
        return s == SIDE_PANDAVA ? "AI  -  flees along BFS routes" : "AI  -  hunts with A* search";
    if (a->mode == MODE_SOLO)
        return ui_touch ? "You  -  tap or use the arrow pad" : "You  -  WASD or Arrow keys";
    KeySet k = s == SIDE_PANDAVA ? a->pandava_keys : a->kaurava_keys;
    if (ui_touch)
        return k == KEYS_WASD ? "Player 1  -  left arrow pad" : "Player 2  -  right arrow pad";
    return k == KEYS_WASD ? "Player 1  -  WASD" : "Player 2  -  Arrow keys";
}

void app_set_screen(App *a, Screen s)
{
    a->screen      = s;
    a->screen_time = 0.0f;
    a->sel         = s == SCREEN_VERSUS ? 2 : 0;
    a->dice_timer  = 0.0f;
    a->keyboard    = false;
    sfx_music(s == SCREEN_BATTLE ? MUSIC_BATTLE : MUSIC_MENU);
}

/* seed 0 = a fresh formation (or the --seed one, the first time). */
void app_start_battle(App *a, uint32_t seed)
{
    if (seed == 0) {
        if (cli_seed_pending) {
            seed = cli_seed;
            cli_seed_pending = false;
        } else {
            seed = (uint32_t)time(NULL) ^ (uint32_t)(GetTime() * 1e6);
        }
    }
    if (a->game_live)
        game_free(&a->game);

    a->seed = seed;
    GameSetup setup = {
        .runner_name = a->pandava.name,
        .chaser_name = a->kaurava.name,
        .runner_ai   = !a->pandava_human || a->autopilot,
        .chaser_ai   = !a->kaurava_human,
        .capture     = a->capture,
    };
    game_init(&a->game, seed, a->rotation_k, FIELD_CX, FIELD_CY, FIELD_R, &setup);
    a->game_live = true;

    /* Keep the overlay toggles; reset everything transient. */
    View keep = a->view;
    a->view = (View){ 0 };
    a->view.show_bfs    = keep.show_bfs;
    a->view.show_astar  = keep.show_astar;
    a->view.show_labels = keep.show_labels;
    a->view.show_help   = keep.show_help;
    a->view.input[0].dir = a->view.input[1].dir = -1;
    a->view.intro = 2.2f;

    app_set_screen(a, SCREEN_BATTLE);
    sfx_play(SFX_CONCH);
}

/* ------------------------------------------------------------------------ */
/* Screenshot tour (--shots)                                                */
/* ------------------------------------------------------------------------ */

typedef struct {
    bool enabled, duel;
    int  stage, frame, rotations_seen, rotate_at, end_at;
    bool done;
} Tour;

/* Two-player tour: P1 takes the Kauravas (WASD), P2 the Pandavas (arrows). */
static void tour_duel(Tour *s)
{
    App *a = &app;
    switch (s->stage) {
    case 0:
        if (s->frame == 30) {
            a->mode = MODE_DUEL;
            app_set_screen(a, SCREEN_SIDES);
            s->stage++, s->frame = 0;
        }
        break;
    case 1:
        if (s->frame == 30) {
            TakeScreenshot("duel_1_sides.png");
            a->p1_side = SIDE_KAURAVA;
            a->pandava_human = a->kaurava_human = true;
            a->kaurava_keys = KEYS_WASD, a->pandava_keys = KEYS_ARROWS;
            a->creating = 0, a->forged[0] = a->forged[1] = false;
            app_set_screen(a, SCREEN_CREATE);
            s->stage++, s->frame = 0;
        }
        break;
    case 2: /* player 1 forges a Kaurava */
        if (s->frame == 5)
            character_from_preset(&a->draft, SIDE_KAURAVA, 1); /* Karna */
        if (s->frame == 30) {
            TakeScreenshot("duel_2_create_p1.png");
            a->kaurava = a->draft, a->forged[0] = true, a->creating = 1;
            app_set_screen(a, SCREEN_CREATE);
            s->stage++, s->frame = 0;
        }
        break;
    case 3: /* player 2 forges a Pandava */
        if (s->frame == 5)
            character_from_preset(&a->draft, SIDE_PANDAVA, 2); /* Bhima */
        if (s->frame == 30) {
            TakeScreenshot("duel_3_create_p2.png");
            a->pandava = a->draft;
            app_set_screen(a, SCREEN_VERSUS);
            s->stage++, s->frame = 0;
        }
        break;
    case 4:
        if (s->frame == 50) {
            TakeScreenshot("duel_4_versus.png");
            app_start_battle(a, 0);
            s->stage++, s->frame = 0;
        }
        break;
    case 5: /* nobody presses keys: both warriors stand still */
        if (s->frame == 200) {
            TakeScreenshot("duel_5_battle.png");
            s->done = true;
        }
        break;
    }
}

static void tour_step(Tour *s)
{
    if (!s->enabled)
        return;
    s->frame++;
    if (s->duel) {
        tour_duel(s);
        return;
    }
    App *a = &app;
    switch (s->stage) {
    case 0: /* title */
        if (s->frame == 60) {
            TakeScreenshot("shot_1_title.png");
            a->mode = MODE_SOLO;
            app_set_screen(a, SCREEN_SIDES);
            s->stage++, s->frame = 0;
        }
        break;
    case 1: /* sides */
        if (s->frame == 40) {
            TakeScreenshot("shot_2_sides.png");
            a->p1_side = SIDE_PANDAVA;
            a->pandava_human = true, a->kaurava_human = false;
            a->pandava_keys = a->kaurava_keys = KEYS_ANY;
            a->creating = 0, a->forged[0] = a->forged[1] = false;
            app_set_screen(a, SCREEN_CREATE);
            s->stage++, s->frame = 0;
        }
        break;
    case 2: /* creator */
        if (s->frame == 5) {
            character_from_preset(&a->draft, SIDE_PANDAVA, 0);
            a->keyboard = ui_touch; /* show the on-screen keyboard in touch mode */
        }
        if (s->frame == 40) {
            TakeScreenshot("shot_3_create.png");
            a->keyboard = false;
            a->pandava = a->draft;
            character_from_preset(&a->kaurava, SIDE_KAURAVA, 2); /* Drona */
            app_set_screen(a, SCREEN_VERSUS);
            s->stage++, s->frame = 0;
        }
        break;
    case 3: /* face-off */
        if (s->frame == 60) {
            TakeScreenshot("shot_4_versus.png");
            a->autopilot = true;
            app_start_battle(a, 0);
            s->stage++, s->frame = 0;
        }
        break;
    case 4: /* battle */
        if (s->frame == 50)
            TakeScreenshot("shot_5_intro.png");
        if (s->frame == 200) {
            a->view.show_bfs = a->view.show_labels = true;
            TakeScreenshot("shot_6_battle.png");
        }
        if (a->game.rotations > s->rotations_seen && s->rotate_at == 0)
            s->rotate_at = s->frame + 45;
        s->rotations_seen = a->game.rotations;
        if (s->frame == s->rotate_at)
            TakeScreenshot("shot_7_rotation.png");
        if (a->game.state != STATE_PLAYING && s->end_at == 0)
            s->end_at = s->frame + 90;
        if (s->frame == s->end_at) {
            TakeScreenshot("shot_8_end.png");
            s->done = true;
        }
        if (s->frame > 60 * 90)
            s->done = true;
        break;
    }
}

/* ------------------------------------------------------------------------ */
/* Main                                                                     */
/* ------------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    Tour tour = { 0 };
    int force_w = 0, force_h = 0;
    app.rotation_k = DEFAULT_ROTATION_K;
    app.view.show_astar = app.view.show_help = true;
    app.rng = (uint32_t)time(NULL) | 1u;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--seed") && i + 1 < argc) {
            cli_seed = (uint32_t)strtoul(argv[++i], NULL, 10);
            cli_seed_pending = true;
        } else if (!strcmp(argv[i], "--k") && i + 1 < argc) {
            app.rotation_k = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--capture")) {
            app.capture = true;
        } else if (!strcmp(argv[i], "--shots")) {
            tour.enabled = true;
        } else if (!strcmp(argv[i], "--shots-duel")) {
            tour.enabled = tour.duel = true;
        } else if (!strcmp(argv[i], "--size") && i + 1 < argc) {
            sscanf(argv[++i], "%dx%d", &force_w, &force_h); /* e.g. a phone: 1600x720 */
        } else if (!strcmp(argv[i], "--touch")) {
            ui_touch = true; /* phone controls on a PC, for testing */
        }
    }
    if (app.rotation_k < MIN_ROTATION_K) app.rotation_k = MIN_ROTATION_K;
    if (app.rotation_k > MAX_ROTATION_K) app.rotation_k = MAX_ROTATION_K;

    /*
     * No FLAG_WINDOW_HIGHDPI: on scaled Windows displays it intermittently
     * left the framebuffer and viewport at different sizes. The camera zoom
     * in ui_camera() already fits the canvas to any window size.
     */
    /*
     * Anti-aliasing is costly on phone GPUs and hardly visible on their dense
     * screens, so touch devices go without it.
     */
    unsigned flags = FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE;
#if defined(__EMSCRIPTEN__)
    if (!web_is_touch())
        flags |= FLAG_MSAA_4X_HINT;
#elif !defined(__ANDROID__)
    flags |= FLAG_MSAA_4X_HINT;
#endif
    SetConfigFlags(flags);
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(SCREEN_W, SCREEN_H, "Chakravyuha");
#if !defined(__ANDROID__)
    SetWindowMinSize(SCREEN_W / 3, SCREEN_H / 3);
#endif
    ui_fit_window();
    if (force_w > 0 && force_h > 0)
        SetWindowSize(force_w, force_h);
#if defined(__EMSCRIPTEN__)
    if (web_is_touch())
        ui_touch = true; /* phones and tablets get the touch controls */
#endif
    SetExitKey(KEY_NULL); /* Esc navigates back instead of quitting */
#if defined(__ANDROID__)
    /* Keep the phone awake while the battle is on screen. */
    ANativeActivity_setWindowFlags(GetAndroidApp()->activity, AWINDOW_FLAG_KEEP_SCREEN_ON, 0);
#endif
    SetTargetFPS(60);

    /* Synthesising the music takes a moment; show a title card meanwhile. */
    ui_init();
    BeginDrawing();
    ClearBackground(C_BG);
    BeginMode2D(ui_camera((Vector2){ 0, 0 }));
    ui_title("CHAKRAVYUHA", SCREEN_W / 2.0f, SCREEN_H / 2.0f - 60, 88, C_GOLD);
    ui_text_center("The drums are being tuned...", SCREEN_W / 2.0f, SCREEN_H / 2.0f + 50, 22,
                   C_DIM);
    EndMode2D();
    EndDrawing();
    sfx_init();

    app_set_screen(&app, SCREEN_TITLE);
#if defined(__EMSCRIPTEN__)
    web_ready(); /* hide the page's loading screen */
    {
        char role[16];
        web_param("test", role, sizeof role);
        webtest.role = !strcmp(role, "host") ? 1 : !strcmp(role, "join") ? 2 : 0;
        webtest.timer = 0.5f;
    }
#endif

    while (!WindowShouldClose() && !app.quit) {
        float dt = GetFrameTime();
        if (dt > 0.1f)
            dt = 0.1f; /* avoid a huge step after a stall */
        float t = (float)GetTime();
        Screen before = app.screen;
#if defined(__EMSCRIPTEN__)
        /* Keep the canvas the size of the browser window (rotation, resizing). */
        if (web_width() != GetScreenWidth() || web_height() != GetScreenHeight())
            SetWindowSize(web_width(), web_height());
#endif

        ui_new_frame();
        sfx_update(dt);
        online_pump(&app, dt);
        BeginDrawing();
        ClearBackground(C_BG);

        if (app.screen == SCREEN_BATTLE) {
            battle_update(&app, dt);
            battle_draw(&app, t);
        } else {
            /* Menus use the standard canvas; the dusk sky fills any margin. */
            DrawRectangleGradientV(0, 0, GetScreenWidth(), GetScreenHeight(),
                                   (Color){ 70, 38, 22, 255 }, C_BG);
            ui_set_canvas_width(SCREEN_W);
            ui_begin(ui_camera((Vector2){ 0, 0 }));
            switch (app.screen) {
            case SCREEN_TITLE:  screen_title(&app, t); break;
            case SCREEN_HOWTO:  screen_howto(&app, t); break;
            case SCREEN_SIDES:  screen_sides(&app, t, dt); break;
            case SCREEN_CREATE: screen_create(&app, t); break;
            case SCREEN_VERSUS: screen_versus(&app, t); break;
            case SCREEN_ONLINE: screen_online(&app, t); break;
            default: break;
            }
            ui_end();
        }

        /* Quick fade in after every screen change. */
        if (app.screen_time < 0.3f)
            DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(),
                          Fade(BLACK, 1.0f - app.screen_time / 0.3f));
        EndDrawing();

        if (app.screen == before)
            app.screen_time += dt;
#if defined(__EMSCRIPTEN__)
        web_set_root(app.screen == SCREEN_TITLE); /* Back there closes the app */
#endif
        tour_step(&tour);
#if defined(__EMSCRIPTEN__)
        webtest_step(&app, dt);
#endif
        if (tour.done)
            break;
    }

    if (app.game_live)
        game_free(&app.game);
    sfx_close();
    ui_close();
    CloseWindow();
    return 0;
}
