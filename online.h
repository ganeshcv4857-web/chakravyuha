/*
 * online.h — Online battles: the lobby screen and the messages exchanged
 * with the other player through the game server (server/server.js).
 *
 * The host's copy of the game is the referee. The guest sends its key
 * presses ("IN|dir"); the host checks each move and broadcasts every
 * accepted move ("MV|side|node|rotations") in order, and both copies apply
 * them with game_force_move. Since a formation is fully determined by its
 * seed and the order of moves, both players always see the same battle.
 */
#ifndef ONLINE_H
#define ONLINE_H

#include "app.h"

void screen_online(App *a, float t);  /* host / join / waiting lobby */
void online_pump(App *a, float dt);   /* handle server messages; every frame */

void online_after_forge(App *a);      /* the local warrior is ready */
void online_begin_battle(App *a, uint32_t seed); /* host: blow the conch */
void online_leave(App *a);            /* back to the title, disconnecting */

/* Battle hooks used by render.c. */
void online_send_input(int dir);      /* guest: a direction was pressed */
void online_broadcast_moves(App *a, int runner_before, int chaser_before);

#endif /* ONLINE_H */
