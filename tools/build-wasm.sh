#!/usr/bin/env bash
#
# Build the browser visualiser: the firmware's own scene and animation code
# compiled to WebAssembly, plus the JavaScript glue that feeds it microphone
# samples. Output lands in web/live/.
#
# Needs Emscripten on PATH. Nothing else in this repo does, which is why this is a
# shell script rather than a PlatformIO environment: Emscripten is not one of
# PlatformIO's platforms and emulating it there would fight the tool.
#
#     source /path/to/emsdk/emsdk_env.sh
#     tools/build-wasm.sh
#
# FastLED comes out of the native environment's library directory. Run
# `pio run -e native` once first if it is not there.
#
# Sources are compiled to objects under web/live/.obj and cached, so a change to
# one file recompiles that file and relinks rather than rebuilding all 128
# FastLED sources. That is the difference between a few seconds and a few
# minutes, and it is the whole reason this script does the compile step itself
# instead of handing one source list to a single em++ call. Pass --clean to throw
# the cache away.
#
#     tools/build-wasm.sh --watch
#
# builds once and then rebuilds whenever a source or header changes, which is the
# loop to leave running while editing an animation. It costs one link per change
# rather than a full rebuild, because the object cache survives. An open page
# reloads itself when the build lands, so the loop is edit, save, look at the page.

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

watch=0
clean=0
for arg in "$@"; do
  case "$arg" in
    --clean) clean=1 ;;
    --watch) watch=1 ;;
    *)
      echo "unknown option: $arg" >&2
      echo "usage: tools/build-wasm.sh [--clean] [--watch]" >&2
      exit 2
      ;;
  esac
done

if ! command -v em++ >/dev/null 2>&1; then
  echo "em++ is not on PATH. Source emsdk_env.sh first, for example:" >&2
  echo "    source /d/emsdk/emsdk_env.sh" >&2
  exit 1
fi

fastled=""
for candidate in .pio/libdeps/native/FastLED .pio/libdeps/ttgo-t1/FastLED; do
  if [ -d "$candidate/src" ]; then fastled="$candidate/src"; break; fi
done
if [ -z "$fastled" ]; then
  echo "FastLED sources not found under .pio/libdeps. Run 'pio run -e native' first." >&2
  exit 1
fi

arduinofft=""
for candidate in .pio/libdeps/native/arduinoFFT/src .pio/libdeps/ttgo-t1/arduinoFFT/src; do
  if [ -d "$candidate" ]; then arduinofft="$candidate"; break; fi
done
if [ -z "$arduinofft" ]; then
  echo "arduinoFFT sources not found under .pio/libdeps. Run 'pio run -e native' first." >&2
  exit 1
fi

out="web/live"
cache="$out/.obj"
mkdir -p "$out" "$cache"

if [ "$clean" = "1" ]; then
  echo "Clearing the object cache."
  rm -rf "$cache"
  mkdir -p "$cache"
fi

# The Arduino emulation is FastLED's, not sim/stubs. On Emscripten FastLED selects
# its own wasm platform, whose <Arduino.h> is platforms/wasm/compiler/Arduino.h
# forwarding to platforms/stub/Arduino.h. Putting sim/stubs on the include path
# instead would leave two emulations in one translation unit, redefining Serial
# and digitalRead against each other. So the include path points at FastLED's
# compiler directory, and sim/stubs/wasm/prelude.h (force-included, and so ahead
# of everything) adds the few things FastLED's emulation lacks: String, Arduino's
# constrain macro, and the ESP object the controller's memory check reads.

# FastLED is compiled whole, exactly as the native environment compiles it, but
# on Emscripten it selects its own wasm platform rather than the stub one (see
# the __EMSCRIPTEN__ branch in FastLED.h), so its library-level defines are
# required: FASTLED_FORCE_NAMESPACE in particular, without which chipsets.h
# expands FASTLED_CLOCKLESS_CONTROLLER to an unqualified name that the wasm
# platform declares only inside namespace fl. Those defines are taken from
# platforms/wasm/compiler/build_flags.toml in the library.
#
# Its linking flags are deliberately not taken. FastLED's wasm build is a whole
# application: it owns main(), runs the sketch loop from JavaScript through
# Asyncify, and requires pthreads. This module is a library that JavaScript
# steps, so it uses --no-entry and no threads.
#
# Six sources are left out of the glob for the same reason. entry_point.cpp and
# ui.cpp are that application's entry point and its DOM panel, js_bindings.cpp is
# the JSON channel behind that panel and needs FastLED's bundled ArduinoJson,
# js_fetch.cpp and fl/fetch.cpp are its HTTP client, and fs_wasm.cpp needs the
# Emscripten filesystem that the link below switches off. None are reachable from
# the effects library, and the fetch headers do not survive FASTLED_FORCE_NAMESPACE.
skip_re='/(entry_point|ui|js_bindings|js_fetch|fs_wasm|fetch)\.cpp$'
mapfile -t fastled_src < <(find "$fastled" -name '*.cpp' | sort | grep -Ev "$skip_re")

compile_flags=(
  -std=gnu++17
  -O2
  -fno-exceptions
  -fno-rtti
  -DGG_HOST_BUILD
  -DFASTLED_FORCE_NAMESPACE=1
  -DFASTLED_USE_PROGMEM=0
  -DUSE_OFFSET_CONVERTER=0
  -DGL_ENABLE_GET_PROC_ADDRESS=0
  -DFASTLED_ENGINE_EVENTS_MAX_LISTENERS=50
  -DEMSCRIPTEN_HAS_UNBOUND_TYPE_NAMES=0
  -D_REENTRANT=1
  -fno-threadsafe-statics
  -I"$fastled/platforms/wasm/compiler"
  -I"$arduinofft"
  -Isrc
  -I"$fastled"
  -include sim/stubs/wasm/prelude.h
)

link_flags=(
  -sMODULARIZE=1
  -sEXPORT_NAME=createGlitchGlimmer
  -sENVIRONMENT=web
  -sALLOW_MEMORY_GROWTH=1
  -sAUTO_NATIVE_LIBRARIES=0
  -sFILESYSTEM=0
  -sEXPORTED_RUNTIME_METHODS=HEAPU8,HEAPF32,UTF8ToString
  --no-entry
)

# clang reads a response file, which is how the per-file compiles below get these
# without repeating them on every command line. None of the flags contain a
# space, so one per line is unambiguous.
printf '%s\n' "${compile_flags[@]}" > "$cache/flags.rsp"

repo_src=(
  src/wasm_main.cpp
  src/audio/AudioProcessor.cpp
)

# The header stamps are what make the cache safe. Tracking per-object header
# dependencies faithfully is the compiler's job, and asking it to do that on
# every invocation is exactly what made this build slow. Instead: any header
# change invalidates the objects that could have included it, which is coarse but
# never stale. Repo headers and library headers are stamped separately, so
# editing src/audio/AudioFeatures.h does not throw away the 128 FastLED objects.
stamp_out_of_date() {
  local stamp="$1"; shift
  [ -f "$stamp" ] || return 0
  [ -n "$(find "$@" \( -name '*.h' -o -name '*.hpp' \) -newer "$stamp" -print -quit 2>/dev/null)" ]
}

lib_stamp="$cache/.lib-stamp"
repo_stamp="$cache/.repo-stamp"

if stamp_out_of_date "$lib_stamp" "$fastled" "$arduinofft"; then
  echo "Library headers changed, rebuilding FastLED objects."
  rm -f "$cache"/lib_*.o
fi
if stamp_out_of_date "$repo_stamp" "$fastled" "$arduinofft" src sim/stubs/wasm; then
  echo "Headers changed, rebuilding project objects."
  rm -f "$cache"/repo_*.o
fi
touch "$lib_stamp" "$repo_stamp"

# One object per source, named from the source path so it is stable across runs.
obj_name() {
  printf '%s_%s.o' "$1" "$(printf '%s' "$2" | tr '/\\.:' '____')"
}

todo="$cache/todo.txt"
: > "$todo"
objects=()

queue() {   # $1 = prefix (lib|repo), $2... = sources
  local prefix="$1"; shift
  local src obj
  for src in "$@"; do
    obj="$cache/$(obj_name "$prefix" "$src")"
    if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ]; then
      printf '%s\n%s\n' "$src" "$obj" >> "$todo"
    fi
    objects+=("$obj")
  done
}

queue lib "${fastled_src[@]}"
queue lib "$arduinofft/arduinoFFT.cpp"
queue repo "${repo_src[@]}"
queue repo src/scenes/*.cpp src/animations/*.cpp

jobs="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
echo "Emscripten: $(em++ --version | head -1)"
echo "FastLED:    $fastled (${#fastled_src[@]} sources)"

pending=$(( $(wc -l < "$todo") / 2 ))
if [ "$pending" -gt 0 ]; then
  echo "Compiling $pending of ${#objects[@]} sources on $jobs jobs."
  # $0 is the response file, $1 the source, $2 the object, because -n 2 hands two
  # arguments to each invocation.
  xargs -d '\n' -n 2 -P "$jobs" bash -c 'em++ @"$0" -c "$1" -o "$2"' \
    "$cache/flags.rsp" < "$todo"
else
  echo "All ${#objects[@]} sources are up to date, linking only."
fi

em++ "${objects[@]}" "${link_flags[@]}" -o "$out/glitchglimmer.js"

# Record the build so an open page can notice it. The module is fetched once at
# load time, so without this a rebuild is invisible until someone reloads by hand,
# which is the slowest part of changing an animation. web/live.js polls this file
# and reloads when the value changes.
#
# Written after the link and not before, so a failed build leaves the previous
# value in place and does not reload a page onto a module that is half written.
# Milliseconds, because two builds inside one second are ordinary in --watch mode.
printf '{"build": %s}\n' "$(date +%s%3N)" > "$out/build.json"

echo
echo "Wrote $out/glitchglimmer.js and $out/glitchglimmer.wasm"
ls -lh "$out"/glitchglimmer.* | awk '{print "  " $9 "  " $5}'

if [ "$watch" != "1" ]; then
  exit 0
fi

# Polling rather than inotify, because inotify is not reliably available under Git
# Bash on Windows and this only has to notice a save within a second. The marker
# lives in the object cache so it shares a filesystem with the sources, which
# makes the mtime comparison meaningful.
echo
echo "Watching src/ and sim/stubs/wasm/. Ctrl-C to stop. An open page reloads itself."
marker="$cache/.watch-marker"
while true; do
  touch "$marker"
  sleep 1
  if [ -z "$(find src sim/stubs/wasm -type f -newer "$marker" -print -quit 2>/dev/null)" ]; then
    continue
  fi

  # Editors write in bursts, so wait for the writes to stop before compiling.
  sleep 0.5
  echo
  echo "--- change detected, rebuilding $(date +%H:%M:%S) ---"
  started=$SECONDS
  if "$root/tools/build-wasm.sh"; then
    echo "--- rebuilt in $((SECONDS - started))s ---"
  else
    # Kept alive on failure on purpose: one bad edit should not end the loop.
    echo "--- build failed after $((SECONDS - started))s, still watching ---" >&2
  fi
done
