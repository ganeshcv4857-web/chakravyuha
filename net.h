/*
 * net.h — A thin WebSocket client for online battles.
 *
 * In the browser (and the Android app, which wraps the web build) this uses
 * the page's WebSocket. Native desktop builds have no online play: every
 * call is a harmless stub and net_available() returns false.
 *
 * Messages are short text lines; see server/server.js for the protocol.
 */
#ifndef NET_H
#define NET_H

#include <stdbool.h>

typedef enum { NET_OFF, NET_CONNECTING, NET_OPEN, NET_CLOSED } NetState;

bool        net_available(void);
void        net_connect(void);          /* to the game server (see net.c) */
void        net_close(void);
NetState    net_state(void);
void        net_send(const char *line);
bool        net_poll(char *out, int size); /* next received line, if any */
const char *net_server(void);           /* address in use, for display */

#endif /* NET_H */
