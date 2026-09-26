#!/usr/bin/env bash
#
# android/build.sh — Build Chakravyuha as a signed Android APK (no Gradle).
#
#   make android            (or: bash android/build.sh)
#
# Tools are looked up under $ANDROID_TOOLS (default D:/android-tools):
#   ndk/android-ndk-r28c           C compiler for phone CPUs
#   sdk/build-tools/35.0.0         aapt2, zipalign, apksigner
#   sdk/platforms/android-35       android.jar
#   jdk-17                         Java, needed by apksigner and keytool
# raylib is compiled from source ($RAYLIB_SRC, default C:/raylib/raylib/src).
#
# Steps:
#   1. compile raylib, the NativeActivity glue and the game for each ABI
#   2. link lib/<abi>/libmain.so (NativeActivity loads it by that name)
#   3. package the manifest and icons with aapt2, then add the libraries
#   4. zipalign, then sign with apksigner
#
# The signing key is created on first use in $ANDROID_TOOLS/keys. Keep it:
# Android only installs an update if it is signed with the same key.
set -euo pipefail

here="$(cd "$(dirname "$0")/.." && (pwd -W 2>/dev/null || pwd))"
ROOT="$here"
TOOLS="${ANDROID_TOOLS:-D:/android-tools}"
NDK="${ANDROID_NDK:-$TOOLS/ndk/android-ndk-r28c}"
BUILD_TOOLS="${ANDROID_BUILD_TOOLS:-$TOOLS/sdk/build-tools/35.0.0}"
PLATFORM_JAR="${ANDROID_PLATFORM_JAR:-$TOOLS/sdk/platforms/android-35/android.jar}"
JAVA="${JAVA:-$TOOLS/jdk-17/bin/java.exe}"
KEYTOOL="$(dirname "$JAVA")/keytool.exe"
RAYLIB_SRC="${RAYLIB_SRC:-C:/raylib/raylib/src}"
KEYSTORE="${KEYSTORE:-$TOOLS/keys/chakravyuha-release.jks}"
KEYPASS="${KEYSTORE%.jks}.pass"

MIN_SDK=24     # Android 7.0
TARGET_SDK=34  # Android 14
VERSION_NAME="$(sed -n 's/^#define GAME_VERSION "\(.*\)"/\1/p' "$ROOT/app.h")"
VERSION_CODE="${VERSION_CODE:-1}"
ABIS="${ABIS:-arm64-v8a armeabi-v7a x86_64}"

BIN="$NDK/toolchains/llvm/prebuilt/windows-x86_64/bin"
CLANG="$BIN/clang.exe"
STRIP="$BIN/llvm-strip.exe"
GLUE="$NDK/sources/android/native_app_glue"
AAPT2="$BUILD_TOOLS/aapt2.exe"
ZIPALIGN="$BUILD_TOOLS/zipalign.exe"
APKSIGNER=("$JAVA" -jar "$BUILD_TOOLS/lib/apksigner.jar")

GAME_SRC="main.c menu.c render.c portrait.c ui.c sfx.c online.c net.c graph.c pathfind.c entity.c game.c unionfind.c character.c"
RAYLIB_MODULES="rcore rshapes rtextures rtext rmodels raudio"

OUT="$ROOT/build/android"
APK="$ROOT/dist/Chakravyuha-v$VERSION_NAME-android-native.apk"

target_for() {
    case "$1" in
    arm64-v8a)   echo "aarch64-linux-android$MIN_SDK" ;;
    armeabi-v7a) echo "armv7a-linux-androideabi$MIN_SDK" ;;
    x86_64)      echo "x86_64-linux-android$MIN_SDK" ;;
    x86)         echo "i686-linux-android$MIN_SDK" ;;
    *)           echo "unknown ABI $1" >&2; exit 1 ;;
    esac
}

for need in "$CLANG" "$AAPT2" "$ZIPALIGN" "$PLATFORM_JAR" "$JAVA" "$RAYLIB_SRC/rcore.c"; do
    [ -e "$need" ] || { echo "missing: $need  (see README: Building the Android APK)"; exit 1; }
done

rm -rf "$OUT"
mkdir -p "$OUT/apk" "$ROOT/dist"
# The recorded conch is compiled in (res/conch_mp3.h is generated, not stored).
[ -f "$ROOT/res/conch_mp3.h" ] || python "$ROOT/tools/embed.py" "$ROOT/res/conch.mp3" "$ROOT/res/conch_mp3.h" CONCH_MP3

# ---- 1 & 2: native code for each ABI ---------------------------------------
for abi in $ABIS; do
    target="$(target_for "$abi")"
    obj="$OUT/obj/$abi"
    mkdir -p "$obj" "$OUT/apk/lib/$abi"
    flags=(--target="$target" -O2 -fPIC -ffunction-sections -fdata-sections
           -DPLATFORM_ANDROID -DGRAPHICS_API_OPENGL_ES2 -I"$RAYLIB_SRC" -I"$GLUE")
    [ "$abi" = armeabi-v7a ] && flags+=(-mthumb)

    echo "[$abi] raylib"
    for m in $RAYLIB_MODULES; do
        "$CLANG" "${flags[@]}" -w -c "$RAYLIB_SRC/$m.c" -o "$obj/raylib_$m.o"
    done
    "$CLANG" "${flags[@]}" -w -c "$GLUE/android_native_app_glue.c" -o "$obj/native_app_glue.o"

    echo "[$abi] game"
    for f in $GAME_SRC; do
        "$CLANG" "${flags[@]}" -std=c11 -Wall -Wextra -c "$ROOT/$f" -o "$obj/${f%.c}.o"
    done

    echo "[$abi] link libmain.so"
    # --wrap=fopen lets raylib read files from inside the APK (see rcore_android.c).
    # max-page-size=16384 keeps the library loadable on 16 KB-page devices.
    "$CLANG" --target="$target" -shared -o "$OUT/apk/lib/$abi/libmain.so" "$obj"/*.o \
        -Wl,-soname,libmain.so -u ANativeActivity_onCreate -Wl,--wrap=fopen \
        -Wl,--gc-sections -Wl,--no-undefined -Wl,--build-id=sha1 \
        -Wl,-z,noexecstack -Wl,-z,relro -Wl,-z,now -Wl,-z,max-page-size=16384 \
        -llog -landroid -lEGL -lGLESv2 -lOpenSLES -lm -ldl
    "$STRIP" --strip-unneeded "$OUT/apk/lib/$abi/libmain.so"
done

# ---- 3: package ------------------------------------------------------------
echo "packaging v$VERSION_NAME (code $VERSION_CODE)"
"$AAPT2" compile --dir "$ROOT/android/res" -o "$OUT/res.zip"
"$AAPT2" link -o "$OUT/base.apk" -I "$PLATFORM_JAR" \
    --manifest "$ROOT/android/AndroidManifest.xml" \
    --min-sdk-version "$MIN_SDK" --target-sdk-version "$TARGET_SDK" \
    --version-code "$VERSION_CODE" --version-name "$VERSION_NAME" \
    "$OUT/res.zip"

# aapt2 does not add native libraries; append lib/<abi>/libmain.so ourselves.
python - "$OUT/base.apk" "$OUT/apk" <<'PY'
import os, sys, zipfile
apk, root = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(apk, "a", zipfile.ZIP_DEFLATED) as z:
    for dirpath, _, files in os.walk(os.path.join(root, "lib")):
        for name in sorted(files):
            full = os.path.join(dirpath, name)
            z.write(full, os.path.relpath(full, root).replace(os.sep, "/"))
PY

# ---- 4: align and sign -----------------------------------------------------
"$ZIPALIGN" -f -P 16 4 "$OUT/base.apk" "$OUT/aligned.apk"

if [ ! -f "$KEYSTORE" ]; then
    echo "creating signing key $KEYSTORE"
    mkdir -p "$(dirname "$KEYSTORE")"
    head -c 32 /dev/urandom | od -An -tx1 | tr -d ' \n' | head -c 32 > "$KEYPASS"
    "$KEYTOOL" -genkeypair -keystore "$KEYSTORE" -alias chakravyuha \
        -keyalg RSA -keysize 3072 -validity 10000 \
        -storepass:file "$KEYPASS" -keypass:file "$KEYPASS" \
        -dname "CN=Chakravyuha, O=Chakravyuha DSA Project" >/dev/null
fi

# The key shares the keystore's password, which apksigner then reuses for it.
"${APKSIGNER[@]}" sign --ks "$KEYSTORE" --ks-key-alias chakravyuha \
    --ks-pass "file:$KEYPASS" \
    --v4-signing-enabled false --out "$APK" "$OUT/aligned.apk"
"${APKSIGNER[@]}" verify --print-certs "$APK" | head -n 3

echo "built $APK ($(du -k "$APK" | cut -f1) KB)"
