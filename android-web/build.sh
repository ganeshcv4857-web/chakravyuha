#!/usr/bin/env bash
#
# android-web/build.sh — Build the Android app from the web build (no Gradle).
#
#   make android            (runs web/build.sh first)
#
# The app is a small Java activity (src/) showing the web build in a
# full-screen WebView; the game files ride inside the APK as assets, so the
# app works offline, and online battles use the internet. Tools come from
# $ANDROID_TOOLS (see android/build.sh and the README).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && (pwd -W 2>/dev/null || pwd))"
TOOLS="${ANDROID_TOOLS:-D:/android-tools}"
BUILD_TOOLS="${ANDROID_BUILD_TOOLS:-$TOOLS/sdk/build-tools/35.0.0}"
PLATFORM_JAR="${ANDROID_PLATFORM_JAR:-$TOOLS/sdk/platforms/android-35/android.jar}"
JDK="${JDK:-$TOOLS/jdk-17}"
KEYSTORE="${KEYSTORE:-$TOOLS/keys/chakravyuha-release.jks}"
KEYPASS="${KEYSTORE%.jks}.pass"

MIN_SDK=24
TARGET_SDK=34
VERSION_NAME="$(sed -n 's/^#define GAME_VERSION "\(.*\)"/\1/p' "$ROOT/app.h")"
VERSION_CODE="${VERSION_CODE:-2}"   # 1 was the native 1.0.0 app

WEB="$ROOT/build/web"
OUT="$ROOT/build/android-web"
APK="$ROOT/dist/Chakravyuha-v$VERSION_NAME-android.apk"
JAVA=("$JDK/bin/java.exe")

for need in "$WEB/index.wasm" "$BUILD_TOOLS/aapt2.exe" "$PLATFORM_JAR" "$JDK/bin/javac.exe" "$KEYSTORE"; do
    [ -e "$need" ] || { echo "missing: $need  (build the web version first: make web)"; exit 1; }
done

rm -rf "$OUT"
mkdir -p "$OUT/classes" "$OUT/dex" "$OUT/assets/web" "$ROOT/dist"

echo "java"
"$JDK/bin/javac.exe" -source 8 -target 8 -Xlint:-options -encoding UTF-8 \
    -bootclasspath "$PLATFORM_JAR" -d "$OUT/classes" \
    "$ROOT/android-web/src/com/chakravyuha/game/MainActivity.java"
# d8 turns Java bytecode into Android's dex format (and desugars lambdas).
"${JAVA[@]}" -cp "$BUILD_TOOLS/lib/d8.jar" com.android.tools.r8.D8 --release \
    --min-api "$MIN_SDK" --lib "$PLATFORM_JAR" --output "$OUT/dex" \
    $(find "$OUT/classes" -name '*.class')

echo "assets"
for f in index.html index.js index.wasm index.data manifest.webmanifest icon-192.png font-license.txt; do
    cp "$WEB/$f" "$OUT/assets/web/"
done

echo "packaging v$VERSION_NAME (code $VERSION_CODE)"
"$BUILD_TOOLS/aapt2.exe" compile --dir "$ROOT/android/res" -o "$OUT/res.zip"
"$BUILD_TOOLS/aapt2.exe" link -o "$OUT/base.apk" -I "$PLATFORM_JAR" \
    --manifest "$ROOT/android-web/AndroidManifest.xml" \
    --min-sdk-version "$MIN_SDK" --target-sdk-version "$TARGET_SDK" \
    --version-code "$VERSION_CODE" --version-name "$VERSION_NAME" \
    -A "$OUT/assets" "$OUT/res.zip"

python - "$OUT/base.apk" "$OUT/dex/classes.dex" <<'PY'
import sys, zipfile
with zipfile.ZipFile(sys.argv[1], "a", zipfile.ZIP_DEFLATED) as z:
    z.write(sys.argv[2], "classes.dex")
PY

"$BUILD_TOOLS/zipalign.exe" -f -P 16 4 "$OUT/base.apk" "$OUT/aligned.apk"
"${JAVA[@]}" -jar "$BUILD_TOOLS/lib/apksigner.jar" sign --ks "$KEYSTORE" \
    --ks-key-alias chakravyuha --ks-pass "file:$KEYPASS" \
    --v4-signing-enabled false --out "$APK" "$OUT/aligned.apk"
"${JAVA[@]}" -jar "$BUILD_TOOLS/lib/apksigner.jar" verify "$APK"

echo "built $APK ($(du -k "$APK" | cut -f1) KB)"
