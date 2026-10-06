# Suggested commands

Build & run (build log goes to .scratch/build_log.txt):
  ./run.sh                      # meson compile + exec build/src/island
  meson compile -C build        # manual build
  ./build/src/island            # run
  rm -rf build && meson setup build   # reconfigure from scratch

Tests:
  meson test -C build           # all GL-free unit tests
  meson test -C build dialog    # single suite

Build is debugoptimized (-O2, debug=true, asserts live, warning_level=2). Fix warnings — the build stays warning-clean.

Platform switch: meson setup build-dc --cross-file sh4-dreamcast-kos -Dplatform=dreamcast (desktop|psp|dreamcast).

Darwin util notes: BSD userland (no GNU timeout by default); rg for search; lldb for crash triage. For memory bugs, one-off ASan build works: clang -fsanitize=address -g -Isrc/core -Isubprojects/cglm-0.9.6/include tests/X.c src/core/*.c -lm -o .scratch/x-asan