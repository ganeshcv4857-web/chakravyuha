#!/usr/bin/env bash
#
# web/build.sh — Build the browser version of Chakravyuha into build/web.
#
#   make web          (or: bash web/build.sh)
#
# Uses the Emscripten SDK that ships with the raylib installer (C:/raylib/emsdk)
# and raylib's prebuilt web library. The output is a static site
# (index.html/js/wasm/data + PWA files) that any web host can serve.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && (pwd -W 2>/dev/null || pwd))"
EMSDK="${EMSDK:-C:/raylib/emsdk}"
RAYLIB_SRC="${RAYLIB_SRC:-C:/raylib/raylib/src}"
PY="$EMSDK/python/3.13.3_64bit/python.exe"
EMCC=("$PY" "$EMSDK/upstream/emscripten/emcc.py")
export EM_CONFIG="$EMSDK/.emscripten"

OUT="$ROOT/build/web"
SRC="main.c menu.c render.c portrait.c ui.c sfx.c online.c net.c graph.c pathfind.c entity.c game.c unionfind.c character.c"

mkdir -p "$OUT"
# The recorded conch is compiled in (res/conch_mp3.h is generated, not stored).
[ -f "$ROOT/res/conch_mp3.h" ] || python "$ROOT/tools/embed.py" "$ROOT/res/conch.mp3" "$ROOT/res/conch_mp3.h" CONCH_MP3
cd "$ROOT"

# -sASYNCIFY lets the ordinary `while (!WindowShouldClose())` loop run in a
# browser: raylib yields to the page once per frame.
"${EMCC[@]}" -std=c11 -O2 -Wall -Wextra -DPLATFORM_WEB -I"$RAYLIB_SRC" $SRC \
    "$RAYLIB_SRC/libraylib.web.a" \
    -sUSE_GLFW=3 -sASYNCIFY -sASYNCIFY_STACK_SIZE=65536 -sSTACK_SIZE=1048576 \
    -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=67108864 \
    --preload-file res/fonts/CrimsonText-Regular.ttf@/fonts/CrimsonText-Regular.ttf \
    --preload-file res/fonts/CrimsonText-Bold.ttf@/fonts/CrimsonText-Bold.ttf \
    --shell-file web/shell.html -o "$OUT/index.html"

cp web/manifest.webmanifest web/sw.js "$OUT/"
cp android/res/mipmap-xxxhdpi/ic_launcher.png "$OUT/icon-192.png"
cp res/icon_512.png "$OUT/icon-512.png"
cp res/fonts/OFL.txt "$OUT/font-license.txt"
cp web/vercel.json "$OUT/"
cp web/vercelignore "$OUT/.vercelignore"   # never upload env files
rm -f "$OUT"/.env*

echo "built $OUT ($(du -sk "$OUT" | cut -f1) KB)"
