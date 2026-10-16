# Source this. wine_dummy_audio: make the SDL of a Windows build in $WINEPREFIX use its
# "dummy" audio driver (headless runs). Wine does not pass SDL_* variables of the
# caller's environment on, so the variable goes into the prefix's registry (HKCU\Environment).
wine_dummy_audio() {
  [ -n "${WINEPREFIX:-}" ] || return 0
  [ -e "$WINEPREFIX/.sdl-dummy-audio" ] && return 0
  wine reg add 'HKCU\Environment' /v SDL_AUDIODRIVER /d dummy /f >/dev/null 2>&1 \
    && touch "$WINEPREFIX/.sdl-dummy-audio"
  return 0
}
