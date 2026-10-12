# Chakravyuha — build with GNU make + gcc (e.g. the w64devkit bundled with raylib).
#
#   make           build the game for development (with a console for the event log)
#   make run       build and run the game
#   make test      headless tests: graph dump + pathfinding/union-find/rules
#   make dump      Phase 1 graph check: print every adjacency list
#   make release   Windows release: dist/Chakravyuha-v<version>-windows.zip
#   make web       browser version in build/web (Emscripten; online play)
#   make android   Android app: the web version in a WebView (online + offline)
#   make android-native   the older fully native Android build (offline only)
#                  (Android/web targets: run from Git Bash; see README)
#   make icons     re-render the icons in res/ and android/res/
#   make clean
#
# If raylib is not on the compiler's default paths, point RAYLIB at the
# folder holding raylib.h and libraylib.a:   make RAYLIB=C:/raylib/raylib/src

# make's built-in default CC is "cc"; prefer gcc unless the user overrides it.
ifeq ($(origin CC),default)
    CC := gcc
endif

CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -O2
RAYLIB ?= C:/raylib/raylib/src
PYTHON ?= python

ifeq ($(OS),Windows_NT)
    EXE        := .exe
    RAYLIB_LIB := -lraylib -lopengl32 -lgdi32 -lwinmm
else
    EXE        :=
    RAYLIB_LIB := -lraylib -lGL -lpthread -ldl -lrt -lX11
endif

# The version lives in app.h (GAME_VERSION) and names the release files.
VERSION := $(shell sed -n 's/^\#define GAME_VERSION "\(.*\)"/\1/p' app.h)

# Core modules: no raylib, shared by the game and the tests.
CORE_SRC := graph.c pathfind.c entity.c game.c unionfind.c character.c
CORE_HDR := graph.h pathfind.h entity.h game.h unionfind.h character.h

# Front end: the only files that use raylib.
UI_SRC := main.c menu.c render.c portrait.c ui.c sfx.c online.c net.c
UI_HDR := app.h portrait.h ui.h sfx.h online.h net.h res/conch_mp3.h

GAME := chakravyuha$(EXE)
DUMP := graph_dump$(EXE)
TEST := test_all$(EXE)

.PHONY: all run test dump release web android android-native icons clean

all: $(GAME)

$(GAME): $(UI_SRC) $(UI_HDR) $(CORE_SRC) $(CORE_HDR)
	$(CC) $(CFLAGS) -I$(RAYLIB) -o $@ $(UI_SRC) $(CORE_SRC) -L$(RAYLIB) $(RAYLIB_LIB) -lm

run: $(GAME)
	./$(GAME)

dump: $(DUMP)

$(DUMP): tests/graph_dump.c graph.c graph.h
	$(CC) $(CFLAGS) -o $@ tests/graph_dump.c graph.c -lm

$(TEST): tests/test_all.c $(CORE_SRC) $(CORE_HDR)
	$(CC) $(CFLAGS) -o $@ tests/test_all.c $(CORE_SRC) -lm

test: $(DUMP) $(TEST)
	./$(DUMP) > /dev/null && echo "graph_dump: OK"
	./$(TEST)

# ---- Windows release: no console window, icon + version info, zipped --------
WIN_DIR := build/windows
WIN_EXE := $(WIN_DIR)/Chakravyuha.exe
WIN_ZIP := dist/Chakravyuha-v$(VERSION)-windows.zip

release: $(WIN_ZIP)

$(WIN_DIR)/resources.o: res/chakravyuha.rc res/chakravyuha.ico
	mkdir -p $(WIN_DIR)
	windres --include-dir res -O coff -i res/chakravyuha.rc -o $@

$(WIN_EXE): $(UI_SRC) $(UI_HDR) $(CORE_SRC) $(CORE_HDR) $(WIN_DIR)/resources.o
	$(CC) $(CFLAGS) -DNDEBUG -mwindows -s -I$(RAYLIB) -o $@ $(UI_SRC) $(CORE_SRC) \
		$(WIN_DIR)/resources.o -L$(RAYLIB) $(RAYLIB_LIB) -lm

$(WIN_ZIP): $(WIN_EXE) packaging/README.txt packaging/assets-README.txt tools/package_windows.py
	$(PYTHON) tools/package_windows.py $(VERSION)

# ---- Web and Android --------------------------------------------------------
web:
	bash web/build.sh

android: web
	bash android-web/build.sh

android-native:
	bash android/build.sh

# ---- Icons: rendered from the same emblem for every platform ----------------
icons:
	mkdir -p build
	$(CC) -std=c11 -O2 -I$(RAYLIB) -o build/make_icon$(EXE) tools/make_icon.c \
		-L$(RAYLIB) $(RAYLIB_LIB) -lm
	./build/make_icon$(EXE)
	$(PYTHON) tools/make_ico.py .

clean:
	rm -rf $(GAME) $(DUMP) $(TEST) build screenshot_*.png shot_*.png duel_*.png

# The recorded conch is compiled into the game from res/conch.mp3.
res/conch_mp3.h: res/conch.mp3 tools/embed.py
	$(PYTHON) tools/embed.py res/conch.mp3 $@ CONCH_MP3
