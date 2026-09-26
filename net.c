/*
 * net.c — WebSocket client. The browser does the networking; C only sends
 * and polls text lines through a few small JavaScript bridges.
 */
#include "net.h"

#include <string.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>

/*
 * The server address: ?server=... on the page URL wins (handy for local
 * testing), then ws://localhost:8787 when the page itself is served from
 * this computer, otherwise the deployed game server.
 */
EM_JS(void, js_net_connect, (void), {
    var q = new URLSearchParams(location.search).get('server');
    var local = location.hostname === 'localhost' || location.hostname === '127.0.0.1';
    var url = q || (local ? 'ws://localhost:8787' : 'wss://chakravyuha-server.onrender.com');
    var net = (window.chakraNet = window.chakraNet || { inbox: [], state: 0, url: url });
    net.url = url;
    net.inbox = [];
    if (net.ws) { try { net.ws.close(); } catch (e) {} }
    net.state = 1; /* connecting */
    try {
        var ws = new WebSocket(url);
        net.ws = ws;
        ws.onopen = function () { if (net.ws === ws) net.state = 2; };
        ws.onmessage = function (e) { if (net.ws === ws) net.inbox.push(String(e.data)); };
        ws.onclose = ws.onerror = function () { if (net.ws === ws) net.state = 3; };
    } catch (e) {
        net.state = 3;
    }
});

EM_JS(void, js_net_close, (void), {
    var net = window.chakraNet;
    if (net && net.ws) { try { net.ws.close(); } catch (e) {} net.ws = null; }
    if (net) net.state = 0;
});

EM_JS(int, js_net_state, (void), {
    return window.chakraNet ? window.chakraNet.state : 0;
});

EM_JS(void, js_net_send, (const char *line), {
    var net = window.chakraNet;
    if (net && net.ws && net.state === 2) net.ws.send(UTF8ToString(line));
});

/* Copies the oldest received line into out; returns its length or -1. */
EM_JS(int, js_net_poll, (char *out, int size), {
    var net = window.chakraNet;
    if (!net || net.inbox.length === 0) return -1;
    var line = net.inbox.shift();
    stringToUTF8(line, out, size);
    return lengthBytesUTF8(line);
});

EM_JS(void, js_net_server, (char *out, int size), {
    var net = window.chakraNet;
    stringToUTF8(net ? net.url : "", out, size);
});

bool net_available(void) { return true; }
void net_connect(void) { js_net_connect(); }
void net_close(void) { js_net_close(); }
NetState net_state(void) { return (NetState)js_net_state(); }
void net_send(const char *line) { js_net_send(line); }
bool net_poll(char *out, int size) { return js_net_poll(out, size) >= 0; }

const char *net_server(void)
{
    static char url[128];
    js_net_server(url, sizeof url);
    return url;
}

#else /* native builds: offline only */

bool net_available(void) { return false; }
void net_connect(void) {}
void net_close(void) {}
NetState net_state(void) { return NET_OFF; }
void net_send(const char *line) { (void)line; }
bool net_poll(char *out, int size)
{
    (void)out, (void)size;
    return false;
}
const char *net_server(void) { return ""; }

#endif
