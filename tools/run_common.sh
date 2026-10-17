#!/usr/bin/env bash
# The common part of the launch scripts of a port: sourced by the game's own
# run-sdl.sh (the native build) and run_sdl_wine.sh (the Windows build under Wine),
# which only say what is game-specific:
#
#   GAME_REPO      the game repository (the directory of the script)
#   PROG           the program name (game.cfg: program=)
#   ENVP           the env prefix of the port: GAME_ENV("X") is $<ENVP>_X
#   DATA_DEFAULT   the data directory when $<ENVP>_DATA_DIR is not set
#   DATA_MARKER    a file that the data directory must hold
#   CD_DEFAULT     optional: the disc's cue sheet when $<ENVP>_CD_IMAGE is not set
#   WINE_NAME      Wine only: the name of the prefix under ~/.cache (shared by the
#                  ports of one game)
#   CONFIG_NAME, CONFIG_TEMPLATE
#                  optional: the port's config file, copied from the template into
#                  the write root when missing, and passed as --config-file
#   game_prepare   optional function, run after the options and before the build:
#                  it may change DATA_DIR, CD_IMAGE and WRITE_ROOT ($RUN_KIND is
#                  native or wine)
#
# then calls run_native "$@" or run_wine "$@".  Options (both):
#   --build            build first (scons -Q device=...)
#   --headless[=N]     no window: Xvfb of its own, N screenshots (default 4, every
#                      5 s) in <repo>/build/shots/{sdl,wine}/
#   --keys=SPEC        headless: keys through $<ENVP>_AUTO_KEYS, SPEC = delay_s:Key[~hold_ms][,...]
#                      (SR-SDK/runtime/core/Game_keys.c)
#   -- ARG...          (native) everything after is given to the port
#   --help
# Environment: <ENVP>_DEVICE, _DATA_DIR, _WRITE_ROOT (default <outer dir>/run),
# _CD_IMAGE, and for Wine _MINGW_BIN and _WINEPREFIX (default ~/.cache/$WINE_NAME;
# ~/.wine is never used).

_envp() { local v="${ENVP}_$1"; printf '%s' "${!v:-}"; }

_common_init() {
  OUTER="$(cd "$GAME_REPO/.." && pwd)"
  DATA_DIR="$(_envp DATA_DIR)"; : "${DATA_DIR:=$DATA_DEFAULT}"
  WRITE_ROOT="$(_envp WRITE_ROOT)"; : "${WRITE_ROOT:=$OUTER/run}"
  CD_IMAGE="$(_envp CD_IMAGE)"
  [ -n "$CD_IMAGE" ] || [ -z "${CD_DEFAULT:-}" ] || CD_IMAGE="$(compgen -G "$CD_DEFAULT" | head -n 1 || true)"
  DATA_DIR="$(realpath -m "$DATA_DIR")"
  [ -z "$CD_IMAGE" ] || CD_IMAGE="$(realpath -m "$CD_IMAGE")"
  HEADLESS=0; BUILD=0; N_SHOTS=4; AUTO_KEYS=""; PORT_ARGS=()
}

_parse_args() {
  while [ $# -gt 0 ]; do
    local arg="$1"; shift
    case "$arg" in
      --) PORT_ARGS=("$@"); break ;;
      --help|-h) awk 'NR > 1 && /^#/ { sub(/^# ?/, ""); print; next } NR > 1 { exit }' "$0"; exit 0 ;;
      --headless=*) HEADLESS=1; N_SHOTS="${arg#*=}" ;;
      --headless)   HEADLESS=1 ;;
      --build)      BUILD=1 ;;
      --keys=*)     AUTO_KEYS="${arg#*=}" ;;
      *) echo "Unknown argument: $arg (see --help)" >&2; exit 1 ;;
    esac
  done
}

_check_data() {
  [ -d "$GAME_REPO/game" ] || { echo "Not a game repository: $GAME_REPO" >&2; exit 1; }
  [ -f "$DATA_DIR/$DATA_MARKER" ] || { echo "Game data not found: $DATA_DIR/$DATA_MARKER (set ${ENVP}_DATA_DIR)" >&2; exit 1; }
}

_build() { # device
  if [ "$BUILD" -eq 1 ] || [ ! -e "$BIN" ]; then
    command -v scons >/dev/null 2>&1 || { echo "scons is not installed" >&2; exit 1; }
    echo "Build: scons -Q device=$1 in $GAME_REPO"
    (cd "$GAME_REPO" && scons -Q device="$1")
  fi
  [ -e "$BIN" ] || { echo "Not built: $BIN (run with --build)" >&2; exit 1; }
}

# --config-file for a port that has a config file
_config_arg() {
  [ -n "${CONFIG_NAME:-}" ] || return 0
  local i given=0
  for i in "${!PORT_ARGS[@]}"; do
    case "${PORT_ARGS[$i]}" in
      --config-file=/*) given=1 ;;
      --config-file=*) given=1; PORT_ARGS[$i]="--config-file=$PWD/${PORT_ARGS[$i]#--config-file=}" ;;
    esac
  done
  [ "$given" -eq 0 ] || return 0
  local cfg="$WRITE_ROOT/$CONFIG_NAME"
  if [ ! -f "$cfg" ]; then
    mkdir -p "$WRITE_ROOT"
    cp "$GAME_REPO/$CONFIG_TEMPLATE" "$cfg"
    echo "Created $cfg (a copy of $CONFIG_TEMPLATE)"
  fi
  PORT_ARGS=("--config-file=$cfg" "${PORT_ARGS[@]+"${PORT_ARGS[@]}"}")
}

_port_env() { # write root as the port sees it
  PORT_ENV=("${ENVP}_DATA_DIR=$DATA_DIR" "${ENVP}_WRITE_ROOT=$1")
  [ -z "$CD_IMAGE" ] || PORT_ENV+=("${ENVP}_CD_IMAGE=$CD_IMAGE")
  [ -z "$AUTO_KEYS" ] || PORT_ENV+=("${ENVP}_AUTO_KEYS=$AUTO_KEYS" "${ENVP}_TRACE_KEYS=1")
}

_shots() { # display pid
  local i
  for i in $(seq 1 "$N_SHOTS"); do
    sleep 5
    DISPLAY="$1" import -window root "$SHOTS/shot_$(printf '%02d' "$i").png"
    echo "  shot_$i.png"
  done
}

_stop() { # pid
  kill "$1" 2>/dev/null || true
  local _
  for _ in 1 2 3 4 5; do kill -0 "$1" 2>/dev/null || break; sleep 0.4; done
  kill -9 "$1" 2>/dev/null || true
  sleep 0.5
}

_need_headless_tools() {
  command -v Xvfb >/dev/null 2>&1 || { echo "Xvfb is not installed" >&2; exit 1; }
  command -v import >/dev/null 2>&1 || { echo "ImageMagick (import) is not installed" >&2; exit 1; }
}

# Xvfb on a free display number (other displays, the user's among them, are left
# alone); sets XVFB_PID and XDISPLAY
_start_xvfb() {
  local dfile; dfile="$(mktemp)"
  Xvfb -displayfd 3 -screen 0 1024x768x24 3>"$dfile" >/dev/null 2>&1 &
  XVFB_PID=$!
  local _
  for _ in $(seq 1 50); do [ -s "$dfile" ] && break; sleep 0.1; done
  [ -s "$dfile" ] || { echo "Xvfb did not start" >&2; rm -f "$dfile"; exit 1; }
  XDISPLAY=":$(head -n 1 "$dfile")"; rm -f "$dfile"
}

run_native() {
  RUN_KIND=native
  _common_init; _parse_args "$@"
  case "$(uname -m)" in aarch64|arm64) local dev=arm64-linux ;; *) local dev=pc64-linux ;; esac
  local device="$(_envp DEVICE)"; : "${device:=$dev}"
  BIN="$GAME_REPO/build/bin/$device/$PROG"
  SHOTS="$GAME_REPO/build/shots/sdl"
  ! declare -F game_prepare >/dev/null || game_prepare
  _check_data
  _build "$device"
  _config_arg
  _port_env "$WRITE_ROOT"
  mkdir -p "$WRITE_ROOT"

  if [ "$HEADLESS" -eq 0 ]; then
    echo "Run: $BIN (${ENVP}_DATA_DIR=$DATA_DIR, ${ENVP}_WRITE_ROOT=$WRITE_ROOT${CD_IMAGE:+, ${ENVP}_CD_IMAGE=$CD_IMAGE})"
    cd "$WRITE_ROOT"
    exec env "${PORT_ENV[@]}" "$BIN" "${PORT_ARGS[@]+"${PORT_ARGS[@]}"}"
  fi

  _need_headless_tools
  mkdir -p "$SHOTS"
  LOGF="$(mktemp)"
  _start_xvfb
  trap 'kill -9 $XVFB_PID 2>/dev/null || true; [ -z "${PID:-}" ] || kill -9 $PID 2>/dev/null || true; rm -f "$LOGF"' EXIT
  cd "$WRITE_ROOT"
  env -u WAYLAND_DISPLAY DISPLAY="$XDISPLAY" SDL_VIDEODRIVER=x11 SDL_AUDIODRIVER=dummy \
    "${PORT_ENV[@]}" "$BIN" "${PORT_ARGS[@]+"${PORT_ARGS[@]}"}" > "$LOGF" 2>&1 &
  PID=$!
  echo "$PROG (pid $PID) on Xvfb $XDISPLAY, screenshots in $SHOTS"
  [ -z "$AUTO_KEYS" ] || echo "  ${ENVP}_AUTO_KEYS=$AUTO_KEYS"
  _shots "$XDISPLAY"
  _stop "$PID"
  echo "== output of $PROG:"
  cat "$LOGF"
}

# A wineserver of this prefix that is still running (the game in a window, say): a
# second process would join it, with its display settings and the user's sound.
_prefix_busy() {
  local pid
  for pid in $(pidof wineserver wineserver64 2>/dev/null); do
    tr '\0' '\n' < "/proc/$pid/environ" 2>/dev/null | grep -qxF "WINEPREFIX=$WINEPREFIX" && return 0
  done
  return 1
}

run_wine() {
  RUN_KIND=wine
  _common_init; _parse_args "$@"
  local build_dir="$GAME_REPO/build/bin/pc64-windows"
  BIN="$build_dir/$PROG.exe"
  SHOTS="$GAME_REPO/build/shots/wine"
  local mingw="$(_envp MINGW_BIN)"; : "${mingw:=/usr/x86_64-w64-mingw32/sys-root/mingw/bin}"
  export WINEPREFIX="$(_envp WINEPREFIX)"; : "${WINEPREFIX:=$HOME/.cache/$WINE_NAME}"; export WINEPREFIX
  export WINEDEBUG="${WINEDEBUG:--all}"
  [ "$WINEPREFIX" != "$HOME/.wine" ] || { echo "${ENVP}_WINEPREFIX must not be ~/.wine" >&2; exit 1; }
  command -v wine >/dev/null 2>&1 || { echo "wine is not installed" >&2; exit 1; }
  ! declare -F game_prepare >/dev/null || game_prepare
  _check_data
  _build pc64-windows

  # the DLLs beside the exe, refreshed when the sysroot's copy is newer
  # (SDL2.dll is sdl2-compat and loads SDL3.dll)
  local dll
  for dll in SDL2.dll SDL3.dll SDL2_mixer.dll; do
    if [ -f "$mingw/$dll" ]; then
      [ "$build_dir/$dll" -nt "$mingw/$dll" ] || cp -p "$mingw/$dll" "$build_dir/$dll"
    elif [ ! -f "$build_dir/$dll" ]; then
      echo "No $dll beside the exe or in $mingw (mingw64-SDL2_mixer, or ${ENVP}_MINGW_BIN)" >&2
      exit 1
    fi
  done

  local wwr="$WRITE_ROOT"
  case "$wwr" in /*) wwr="Z:${wwr//\//\\}" ;; esac    # the path as Wine sees it
  _port_env "$wwr"
  mkdir -p "$WRITE_ROOT"
  # Mono/Gecko are not needed; without this a new prefix asks to install them
  local overrides="mscoree=d;mshtml=d"

  if [ "$HEADLESS" -eq 0 ]; then
    [ -n "${DISPLAY:-}" ] || { echo "X11 (DISPLAY) is needed: Wine works through X11/XWayland here" >&2; exit 1; }
    echo "Run under Wine: $BIN (${ENVP}_DATA_DIR=$DATA_DIR, WINEPREFIX=$WINEPREFIX)"
    cd "$WRITE_ROOT"
    exec env -u WAYLAND_DISPLAY WINEDLLOVERRIDES="$overrides" "${PORT_ENV[@]}" wine "$BIN"
  fi

  _need_headless_tools
  local _
  for _ in $(seq 1 10); do _prefix_busy || break; sleep 0.5; done   # an idle wineserver lingers
  if _prefix_busy; then
    echo "Something already runs in $WINEPREFIX (the game in a window?); close it and retry" >&2
    exit 1
  fi
  mkdir -p "$SHOTS"
  . "$(dirname "${BASH_SOURCE[0]}")/wine_dummy_audio.sh"; wine_dummy_audio
  LOGF="$(mktemp)"
  _start_xvfb
  trap 'kill $XVFB_PID 2>/dev/null || true; [ -z "${PID:-}" ] || kill -9 $PID 2>/dev/null || true; rm -f "$LOGF"' EXIT
  cd "$WRITE_ROOT"
  env -u WAYLAND_DISPLAY DISPLAY="$XDISPLAY" SDL_AUDIODRIVER=dummy \
    WINEDLLOVERRIDES="$overrides;winepulse.drv=d;winealsa.drv=d;wineoss.drv=d" \
    "${PORT_ENV[@]}" wine "$BIN" > "$LOGF" 2>&1 &
  PID=$!
  echo "$PROG.exe (pid $PID) under Wine on Xvfb $XDISPLAY, screenshots in $SHOTS"
  [ -z "$AUTO_KEYS" ] || echo "  ${ENVP}_AUTO_KEYS=$AUTO_KEYS"
  _shots "$XDISPLAY"
  _stop "$PID"
  echo "== output of $PROG.exe:"
  cat "$LOGF"
}
