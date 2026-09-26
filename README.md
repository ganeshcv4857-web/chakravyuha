# Chakravyuha

A 2D game in C (C11 + raylib) set on the battlefield of Kurukshetra, where the
gameplay **is** a dynamic graph traversal problem. A Pandava warrior breaches
5 concentric rings of rooms to reach the sacred fire at the core, and a
Kaurava warrior hunts them. Every K gate crossings the formation rotates:
each ring's gate edge is removed from the graph and re-inserted at a
different node. Every route (BFS for the Pandava, A* for the Kaurava) then has
to be found again.

## Build and run

Requires gcc + make + raylib. The official raylib Windows installer provides
all three (`C:\raylib\w64devkit\bin` must be on `PATH`, or use its shell).

```
make            # build chakravyuha.exe
make run        # build and play
make test       # headless tests (no window needed)
make dump       # Phase 1: print every adjacency list, rotate, re-print
```

If raylib lives elsewhere: `make RAYLIB=path/to/folder/with/raylib.h`.

Command-line options: `--seed N` (formation for the first battle), `--k N`
(rotation threshold, default 3), `--capture` (Union-Find gate capture on),
`--touch` (the phone controls, usable with a mouse), `--shots` /
`--shots-duel` (scripted tours that save screenshots, used for testing).
Rotation, breach and A* events are also logged to the console.

## Play it

| Where | What | How to build |
|---|---|---|
| **https://chakravyuha-six.vercel.app** | The website: any browser on a PC or phone; solo, same-device and **online** battles | `make web`, then deploy `build/web` (see below) |
| `dist/Chakravyuha-v1.1.0-android.apk` | Android 7.0+ app: the website packed into an app; plays offline and online | `make android` (from Git Bash) |
| `dist/Chakravyuha-v1.1.0-windows.zip` | Windows 7+ (64-bit): solo and same-keyboard battles | `make release` |

**Windows:** the zip holds `Chakravyuha.exe` (no console window, with an
icon and version info), a player `README.txt`, and an `assets/` folder for an
optional sound pack. Unzip it anywhere and double-click the exe. It needs only
DLLs that ship with Windows. The exe is not code-signed, so SmartScreen may
ask you to click *More info → Run anyway*.

**Android:** copy the APK to the phone and open it. Android asks once to allow
installs from that app (Files, Chrome...). Play Protect may warn about an
unknown developer because the app isn't from the Play Store. Version 1.1.0
installs as an update over 1.0.0.

**Website on a phone:** open the link and turn the phone sideways. The first
tap goes full screen. *Add to Home Screen* installs it like an app, and
after the first visit it opens without a connection (online battles still
need one).

## Online battles

1. One player chooses **Online Battle → Host a battle**, picks an army, forges
   a warrior and gets a **four-letter code**.
2. The other player chooses **Join with a code** and types it in. They lead the
   other army and forge their warrior.
3. The host blows the conch. Either player can be on a phone or a PC.

How it works:
- Both copies of the game run the same formation, from the same seed.
- The guest sends only its key presses and taps to the host.
- The **host's copy is the referee.** It checks each move (cooldowns, edges,
  wake-up) and broadcasts every accepted move to both players in order.
- Because a formation is fully determined by its seed and the order of moves,
  both copies stay identical, including every gate rotation.
- `make test` includes a 200-battle check that a guest replaying the host's
  moves never drifts (`test_online_lockstep`).

The server (`server/server.js`) only pairs players by code and relays their
messages. It is plain Node.js with no packages: the WebSocket handshake and
framing are written out in the file. `node server/test.js` checks it. It runs
on Render's free tier from `render.yaml`. After 15 idle minutes it sleeps, so
the first online battle of the day can take up to a minute to connect.

Local testing: run `node server/server.js` and serve `build/web` from
localhost. The page then uses `ws://localhost:8787`. Anywhere else,
`?server=wss://...` on the page URL chooses a different server.

## Building the web and Android versions

**Web (`web/build.sh`):** compiles the same C code to WebAssembly with the
Emscripten SDK that comes with the raylib installer (`C:/raylib/emsdk`). The
output is a static site in `build/web`: `index.html/js/wasm/data`, the PWA
manifest, a service worker for offline play, and icons. The site bundles the
Crimson Text font (SIL Open Font License, `res/fonts/OFL.txt`) because a
browser can't use Windows fonts.

To deploy with the Vercel CLI (already logged in):
```
cd build/web && vercel deploy --prod
```
`web/vercelignore` keeps any `.env` file out of the upload.

**Android (`android-web/build.sh`):** a small Java activity
(`android-web/src/.../MainActivity.java`) shows the web build in a
full-screen WebView. The game files are packed in the APK and served from a
private `https://appassets.chakravyuha/` address. The activity passes the Back
button to the game, keeps the screen on and pauses the game in the
background. The script uses `javac`, `d8`, `aapt2`, `zipalign` and
`apksigner`, with no Gradle. It expects these tools under `ANDROID_TOOLS`
(default `D:/android-tools`):

| Folder | Download |
|---|---|
| `sdk/build-tools/35.0.0` | `build-tools_r35_windows.zip` (dl.google.com) |
| `sdk/platforms/android-35` | `platform-35_r02.zip` (dl.google.com) |
| `jdk-17` | Microsoft OpenJDK 17 zip |
| `ndk/android-ndk-r28c` | only for `make android-native` (the 1.0.0 fully native build) |

The signing key lives in `ANDROID_TOOLS/keys/`, with its password in the
`.pass` file next to it. **Keep both files safe:** Android only accepts an
update to an installed app if it is signed with the same key. The key is
deliberately kept outside the project folder.

To release a new version:
1. Bump `GAME_VERSION` in `app.h`, the version in `res/chakravyuha.rc` and
   `CACHE` in `web/sw.js`.
2. Build Android with a higher code: `VERSION_CODE=3 make android`.

`make icons` re-renders every icon from `tools/make_icon.c`.

## Playing

1. **Title:** choose **Solo Battle**, **Two Warriors** (two players on one
   keyboard or one phone) or, on the website and Android app, **Online
   Battle**. Mouse, keyboard and touch all work in every menu.
2. **Choose your side:** the **Pandavas** breach the formation and the
   **Kauravas** guard it. You can also let **Shakuni's dice** decide.
   - Solo: the AI takes the other side. A Kaurava AI hunts with A*; a Pandava
     AI flees along BFS distance maps.
   - Two warriors: Player 1 uses **WASD** (the left pad on a phone) and
     Player 2 the **arrow keys** (the right pad).
3. **Forge your warrior:** type a name, pick one of the legends (Abhimanyu,
   Arjuna, Bhima... / Duryodhana, Karna, Drona, Jayadratha...) or customise
   skin, hair, headgear (mukut crown, turban, war helmet, peacock circlet),
   facial hair, tilak, kundala earrings, armour and expression. The portrait
   is drawn from these choices and appears on the face-off screen, beside the
   battlefield and as your token on it.
4. **Face-off:** set K and gate capture, then **Blow the conch!**

The Pandava wins by reaching the core. The Kaurava wins by landing on the
Pandava's node. Every K gate crossings, by either side, the gates rotate and
the Kaurava gets a little faster.

### Controls in battle

| Key | Action |
|---|---|
| A / D or Left / Right | move counter-clockwise / clockwise around the ring |
| W or Up | go through a gate inward (only when standing on a gate node) |
| S or Down | go through a gate outward |
| B | show the Pandava's BFS route to the core |
| V | show the Kaurava's A* route and the nodes A* expanded |
| L | node labels (`R2.5` = ring 2, index 5; ring 1 is outermost) |
| [ / ] | decrease / increase the rotation threshold K (offline) |
| G | force a rotation now (for demos, offline) |
| C | toggle gate capture (Union-Find, offline) |
| P | autopilot (solo Pandava only) |
| R / N | rematch this formation / new formation (the host, online) |
| M / H / Esc | mute / hide controls / main menu |

Letter badges (or arrow badges for Player 2) beside each human warrior show
the moves available right now.

### On a phone
On a phone held sideways, the battle uses the whole screen:
- The formation sits in the middle. Each warrior gets a column with a big
  portrait, their progress and a large **arrow pad**. The PC side panel and
  chronicle are left out.
- **Tap a glowing room** next to your warrior to step there. Or use the arrow
  pad: ↺ / ↻ walk around the ring, ▲ goes through a gate inward, ▼ falls back
  outward. Arrows you can't use right now are dimmed.
- In a two-player game each player gets their own pad and column. Both pads
  can be pressed at once.
- Big buttons replace the keyboard shortcuts (route, A* path, labels, capture,
  autopilot, mute, menu).
- Tap your name in the character creator to type it on the on-screen keyboard.
- The Back button works like Esc.
- Performance: phones skip anti-aliasing, and each face is drawn once into a
  cached texture instead of every frame.

## Sound and music

All audio is synthesised at startup in `sfx.c` (about 0.4 s), so the game still
ships as a single executable:

| Sound | Where it comes from |
|---|---|
| Battle and menu music | Dhol and nagara drum patterns, a Karplus-Strong plucked tanpura drone, and a shehnai-like reed playing phrases in a raga scale. Written as seamless loops |
| Breaching a gate | Sword clash (inharmonic metal partials plus an impact and a "shing") |
| Blowing the conch (battle start) and victory | A recorded conch shell (shankh): `res/conch.mp3`, compiled into the game by `tools/embed.py`. Replace that file and rebuild to change it |
| Formation rotates | War-drum roll |
| Kaurava crosses a gate | War horn |
| Movement | Footsteps. The Kaurava's armoured steps get louder and pan toward you as it closes in, and a heartbeat drum sounds when it is two steps away |

**Using a real sound pack:** put audio files (`.wav`, `.ogg`, `.mp3` or
`.flac`) in an `assets/` folder next to the executable and they replace the
synthesised sounds one by one:

```
assets/music/menu.ogg        assets/music/battle.ogg
assets/sfx/step  enemy_step  blocked  clash  clash_heavy  drum_roll  conch
           victory  defeat  capture  enemy_gate  ui_move  ui_select  dice  danger
```

The title screen shows how many pack files were loaded.

## Where the data structures and algorithms are

| File | Contents |
|---|---|
| `graph.h/.c` | `Node`/`Edge`/`Ring`/`Gate` structs, adjacency lists as singly linked lists, ring construction, **gate rotation (edge removal + insertion)**, `graph_validate` invariants |
| `pathfind.h/.c` | **BFS** (array FIFO queue), **BFS distance maps**, and **A\*** (hand-written binary min-heap, Euclidean heuristic). Generic: they only read nodes, adjacency lists and positions |
| `unionfind.h/.c` | **Disjoint-set forest** with path compression + union by rank |
| `entity.h/.c` | Warrior state and node-snapped movement that only follows edges found in the adjacency list |
| `game.h/.c` | Rules: human/AI control of each side, breach counter, rotation trigger, A* replan triggers, the fleeing AI, captures, win/lose, event log |
| `character.h/.c` | Warrior data: look options and the legendary presets |
| `main.c`, `menu.c`, `render.c`, `portrait.c`, `ui.c`, `sfx.c` | raylib front end: screens, battlefield and HUD (desktop and phone layouts), procedural portraits, widgets, audio. The only files that call raylib |
| `online.c`, `net.c` | Online battles: lobby, room codes, the host-as-referee move protocol, and a WebSocket bridge to the browser |
| `server/` | The online relay server (Node.js, no dependencies) and its test |
| `web/`, `android-web/` | Web page shell, PWA files, and the Android WebView app |
| `tests/` | `graph_dump.c` (Phase 1 printout) and `test_all.c` (automated checks) |
| `android/`, `res/`, `packaging/`, `tools/` | Release builds: the Android manifest, icons and build script; the Windows icon and version resource; the player readme; the icon renderer and the packaging scripts |

The core modules (`graph`, `pathfind`, `unionfind`, `entity`, `game`,
`character`) contain no raylib calls. The test suite compiles them without
raylib.

### Graph model
- 41 nodes: rings of 12, 10, 8, 6 and 4 nodes, plus the core. Node positions
  use `angle = 2π · index / count`.
- Each ring is a **cycle graph**: node *i* is joined to *i±1 mod n* (40 ring
  edges in total).
- **Exactly one gate edge** joins ring *r* to ring *r+1*, and one joins ring 5
  to the core (5 gate edges). The inner end of a gate is the node on the next
  ring closest in angle to its outer end.
- Undirected edges are stored twice, once in each endpoint's list.

### Gate rotation (`graph_rotate_gates`)
For each gate that isn't locked: remove the gate edge from both adjacency
lists, move the outer end forward by a random offset in `[1, n-1]` (so it
always lands on a different node), then insert the new edge. After a
rotation, `game_rotate` recomputes the Pandava's BFS route and forces an A*
replan for the Kaurava.

### BFS (`bfs_path`, `bfs_distances`), O(V + E)
FIFO queue with a `discovered[]` array and a `parent[]` array. The path is
rebuilt by following `parent[]` back from the goal. `bfs_distances` runs the
same traversal without a goal and records every node's hop distance.

### A\* (`astar_path`)
- `g` = cost of the best path found so far. Edge cost is the Euclidean length
  between node positions.
- `h` = straight-line distance to the goal.
- The open set is a binary min-heap ordered by `f = g + h`. When a node gets
  a cheaper `g`, it is pushed again, and the stale heap entry is skipped when
  popped (lazy deletion).
- Edge costs are Euclidean lengths, so `h` is consistent and the result is
  optimal.

The AI Kaurava re-runs A* when:
1. a rotation happens,
2. it reaches the node its current route was planned to, or
3. the Pandava breaches a gate. This trigger is extra; without it the route
   would still point at the ring the Pandava just left.

A human Kaurava gets the same A* route as a "scout" overlay (V), refreshed
after each move.

### The fleeing Pandava AI (`game_autopilot_next`)
Two BFS distance maps are computed each step, one from the core and one from
the Kaurava. The AI scores holding position and each neighbour. It never
steps onto the Kaurava, avoids nodes the Kaurava could reach next move,
prefers the node closest to the core, and breaks ties by distance from the
Kaurava.

### Union-Find (`--capture` / `C`)
The elements are the rings plus the core. When capture mode is on and the
Pandava crosses gate *r* inward, rings *r* and *r+1* are unioned. Before
every rotation, a gate is locked exactly when `find(r) == find(r+1)`, so
captured gates never move. The HUD shows the rings in the same set as ring 1.

## Tests (`make test`)
- **Graph invariants:** checked after 10,000 rotations. Each rotation must
  remove the old gate edge and insert the new one.
- **BFS and A\* vs a reference Dijkstra:** all 41×41 node pairs, after each of
  200 rotations. BFS must find the fewest-edge path and A* the shortest-length
  path, and every step of both must be a real edge. On average A* expands
  ~16 nodes where Dijkstra expands ~21.
- **BFS distance maps:** checked against single-pair BFS on every pair.
- **Movement:** direction keys resolve only to edges that exist in the
  adjacency list. A human Kaurava must wait to wake, obey its cooldown and
  never move without input.
- **Rotation rules:** a rotation happens after exactly every K crossings for
  K = 1..4, every rotation causes an A* replan, and captured gates never move.
- **Balance:** 300 full games are simulated with the Pandava AI, both against
  the A* Kaurava and against a scripted "human" Kaurava.
