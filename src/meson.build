# Core and backend sources compile together, but the directories are the hard
# boundary: core only talks to the RenderAPI (render.h) and the input contract
# (input.h); each backend owns its platform API (GL/GLFW or native KOS PVR).
#   desktop -> GLFW + OpenGL 2.1 compat (glad)
#   psp     -> pspsdk + sceGu/sceGum (native GE), packaged to EBOOT.PBP

opt_platform = get_option('platform')

core_inc = include_directories('core')
core_sources = files(
  'core/camera.c',
  'core/car.c',
  'core/chase_cam.c',
  'core/dialog.c',
  'core/frustum.c',
  'core/game.c',
  'core/input.c',
  'core/mesh.c',
  'core/render.c',
  'core/residents.c',
  'core/terrain.c',
  'core/texture.c',
)

if opt_platform == 'desktop'

  backend_inc = include_directories('backend/desktop', 'backend/common')
  backend_sources = files(
    'backend/desktop/gfx_gl.c',
    'backend/desktop/input_glfw.c',
    'backend/desktop/main.c',
    'backend/desktop/render_gl.c',
    'backend/desktop/texture_gl.c',
  )
  platform_deps = [glad_dep, glfw_dep]
  platform_args = ['-DGLFW_INCLUDE_NONE']
  platform_inc = []

  island_exe = executable(
    'island',
    core_sources + backend_sources,
    include_directories: [core_inc, backend_inc, platform_inc],
    dependencies: [cglm_dep, platform_deps],
    c_args: platform_args,
    install: false,
  )

  # Keep the art next to EBOOT.PBP. The loader also searches host0:/assets and
  # ms0:/PSP/GAME/island/assets, so putting it here is just convenient.
  custom_target(
    'psp-copy-assets',
    input: [],
    output: 'assets.stamp',
    # Always re-examined so adds/renames/deletes/content edits under assets/
    # are picked up (input: [] alone never re-ran after the first build).
    # Only recopy + bump the stamp when the tree actually differs, so
    # downstream targets keyed on the stamp don't rebuild on every ninja.
    command: [
      find_program('sh'),
      '-c',
      'if [ -f "$0" ] && diff -rq "$1" "$2/assets" >/dev/null 2>&1; then :; '
      + 'else rm -rf "$2/assets" && cp -R "$1" "$2" && touch "$0"; fi',
      '@OUTPUT@',
      meson.project_source_root() / 'assets',
      meson.current_build_dir(),
    ],
    build_by_default: true,
    build_always_stale: true,
  )
elif opt_platform == 'dreamcast'
  # Native KOS PVR backend. KOS headers come from the toolchain install;
  # the desktop backend dir is on the include path only for the shared
  # stb_image.h decoder.
  backend_inc = include_directories('backend/dreamcast', 'backend/common')
  backend_sources = files(
    'backend/dreamcast/input_dc.c',
    'backend/dreamcast/main_dc.c',
    'backend/dreamcast/render_dc.c',
    'backend/dreamcast/texture_dc.c',
  )
  platform_deps = []
  platform_args = []
  platform_inc = [
    include_directories('backend/desktop'),
    include_directories('/opt/toolchains/dc/kos/include'),
    include_directories('/opt/toolchains/dc/kos/kernel/arch/dreamcast/include'),
    include_directories('/opt/toolchains/dc/kos/addons/include'),
  ]

  # copy_assets (a .stamp) in the source list forces meson to build the asset
  # copy before the binary, without trying to compile the stamp itself.
  island_exe = executable(
    'island.elf',
    core_sources + backend_sources,
    include_directories: [core_inc, backend_inc, platform_inc],
    dependencies: [cglm_dep, platform_deps, math_dep],
    c_args: platform_args,
    install: false,
  )

  # Keep the art next to the binary; the loader's "assets" fallback is the
  # path that lands on the disc.
  dc_copy_assets = custom_target(
    'dc-copy-assets',
    input: [],
    output: 'assets.stamp',
    # Always re-examined so adds/renames/deletes/content edits under assets/
    # are picked up (input: [] alone never re-ran after the first build).
    # Only recopy + bump the stamp when the tree actually differs, so
    # downstream targets keyed on the stamp don't rebuild on every ninja.
    command: [
      find_program('sh'),
      '-c',
      'if [ -f "$0" ] && diff -rq "$1" "$2/assets" >/dev/null 2>&1; then :; '
      + 'else rm -rf "$2/assets" && cp -R "$1" "$2" && touch "$0"; fi',
      '@OUTPUT@',
      meson.project_source_root() / 'assets',
      meson.current_build_dir(),
    ],
    build_by_default: true,
    build_always_stale: true,
  )

  # Package into a DiscJuggler image (1ST_READ.BIN + IP.BIN + assets), ready
  # for Flycast or real hardware. mkdcdisc scrambles the ELF and builds the
  # default IP.BIN itself; -d keeps the assets/ dir name on the disc, which
  # matches the loader's fallback. Mirrors the working recipe in the
  # dc/integrity tree (pkg_dreamcast_cdi.sh).
  mkdcdisc_prog = find_program('mkdcdisc', required: true)
  custom_target(
    'island-cdi',
    input: island_exe,
    output: 'island.cdi',
    command: [
      mkdcdisc_prog,
      '-e', island_exe.full_path(),
      '-d', meson.current_build_dir() / 'assets',
      '-n', 'island',
      '-N', '-v', '3',
      '-o', '@OUTPUT@',
    ],
    depends: [island_exe, dc_copy_assets],
    build_by_default: true,
    install: false,
  )

elif opt_platform == 'psp'

  backend_inc = include_directories('backend/psp', 'backend/common')

  backend_sources = files(
    'backend/psp/input_psp.c',
    'backend/psp/main.c',
    'backend/psp/render_gu.c',
    'backend/psp/texture_gu.c',
  )

  # pspsdk link recipe (matches the psp-dev sample/prx layout): everything is
  # wrapped in a group so static libs resolve, then the PRX specs/relocation
  # table let psp-prxgen turn the ELF into a bootable module.
  psp_link_args = [
    '-Wl,--end-group',
    '-latomic',
    '-lpspgu',
    '-lpspgum',
    '-lpspdebug',
    '-lpspvfpu',
    '-lpspctrl',
    '-lpspge',
    '-lpspdisplay',
    '-lm',
    '-lpspsdk',
    '-lpsprtc',
    '-lpspaudio',
    '-lpsppower',
    '-Wl,--start-group',
    '-lc',
    '-specs=/opt/toolchains/pspdev/psp/sdk/lib/prxspecs',
    '-Wl,-q,-T/opt/toolchains/pspdev/psp/sdk/lib/linkfile.prx', '/opt/toolchains/pspdev/psp/sdk/lib/prxexports.o',
  ]

  island_exe = executable(
    'island',
    core_sources + backend_sources,
    include_directories: [core_inc, backend_inc],
    dependencies: [cglm_dep],
    link_args: psp_link_args,
    install: false,
  )

  # Keep the art next to EBOOT.PBP. The loader also searches host0:/assets and
  # ms0:/PSP/GAME/island/assets, so putting it here is just convenient.
  custom_target(
    'psp-copy-assets',
    input: [],
    output: 'assets.stamp',
    # Always re-examined so adds/renames/deletes/content edits under assets/
    # are picked up (input: [] alone never re-ran after the first build).
    # Only recopy + bump the stamp when the tree actually differs, so
    # downstream targets keyed on the stamp don't rebuild on every ninja.
    command: [
      find_program('sh'),
      '-c',
      'if [ -f "$0" ] && diff -rq "$1" "$2/assets" >/dev/null 2>&1; then :; '
      + 'else rm -rf "$2/assets" && cp -R "$1" "$2" && touch "$0"; fi',
      '@OUTPUT@',
      meson.project_source_root() / 'assets',
      meson.current_build_dir(),
    ],
    build_by_default: true,
    build_always_stale: true,
  )

  # Post-process into a homebrew EBOOT.PBP (title, prx, then the two CFW
  # launcher aliases). Mirrors the working recipe in the dc/platformer tree.
  mksfo = find_program('mksfo', required: true)
  psp_fixup = find_program('psp-fixup-imports', required: true)
  psp_prxgen = find_program('psp-prxgen', required: true)
  pack_pbp = find_program('pack-pbp', required: true)

  sfo = custom_target('island_sfo', output: 'param.sfo', command: [mksfo, 'island', '@OUTPUT@'])

  custom_target(
    'island_psp_pbp',
    input: [island_exe, sfo],
    output: 'EBOOT.PBP',
    command: [
      find_program('sh'),
      '-c', 'd=$(dirname "$2"); '
      + 'psp-fixup-imports "$1" && '
      + 'psp-prxgen "$1" "$1.prx" && '
      + 'pack-pbp "$2" "$3" NULL NULL NULL NULL NULL "$1.prx" NULL && '
      + 'cp -f "$1.prx" "$d/EBOOT.BIN" && '
      + 'cp -f "$1.prx" "$d/BOOT.BIN"',
      'island_psp_pack',
      island_exe.full_path(),
      '@OUTPUT@',
      sfo.full_path(),
    ],
    depends: island_exe,
    build_by_default: true,
    install: false,
  )
else
  error('Unsupported platform: ' + opt_platform)
endif

# GL-free smoke test: core must record a full frame with no backend linked at
# all. This is the "recording stub backend" the RenderAPI task asks for.
# native: true keeps the tests running on the host during cross builds.
api_smoke = executable(
  'api-smoke',
  files('../tests/api_smoke.c') + core_sources,
  include_directories: [core_inc],
  dependencies: [cglm_dep, math_dep],
  install: false,
)
test('api-smoke', api_smoke)

# GL-free test for the player mode state machine (on-foot/in-vehicle).
mode_test = executable(
  'mode-test',
  files('../tests/mode_test.c') + core_sources,
  include_directories: [core_inc],
  dependencies: [cglm_dep, math_dep],
  install: false,
)
test('mode', mode_test)

# GL-free test for the MVP delivery loop (island1-pkcj).
quest_test = executable(
  'quest-test',
  files('../tests/quest_test.c') + core_sources,
  include_directories: [core_inc],
  dependencies: [cglm_dep, math_dep],
  install: false,
)
test('quest', quest_test)

# GL-free test for the platform-agnostic input contract (actions/axes/edges).
input_test = executable(
  'input-test',
  files('../tests/input_test.c', 'core/input.c'),
  include_directories: [core_inc],
  dependencies: [math_dep],
  install: false,
)
test('input', input_test)

# GL-free behaviour test for the chase camera spring/lag.
chase_cam_test = executable(
  'chase-cam-test',
  files('../tests/chase_cam_test.c', 'core/chase_cam.c'),
  include_directories: [core_inc],
  dependencies: [cglm_dep, math_dep],
  install: false,
)
test('chase-cam', chase_cam_test)

# GL-free behaviour test for the hover-car physics (island1-9n0n).
car_test = executable(
  'car-test',
  files('../tests/car_test.c', 'core/car.c'),
  include_directories: [core_inc],
  dependencies: [cglm_dep, math_dep],
  install: false,
)
test('car', car_test)

# GL-free behaviour test for the conversation textbox typewriter (island1-a2xf).
dialog_test = executable(
  'dialog-test',
  files('../tests/dialog_test.c', 'core/dialog.c', 'core/render.c'),
  include_directories: [core_inc],
  dependencies: [math_dep],
  install: false,
)
test('dialog', dialog_test)

# GL-free test for the shared frustum helpers: extraction must reject on all
# six planes, and the quad test must be strictly tighter than the AABB test
# (island1-762c).
frustum_test = executable(
  'frustum-test',
  files('../tests/frustum_test.c', 'core/frustum.c'),
  include_directories: [core_inc],
  dependencies: [cglm_dep, math_dep],
  install: false,
)
test('frustum', frustum_test)

# GL-free test for terrain's per-island frustum + fog culling (island1-762c).
terrain_cull_test = executable(
  'terrain-cull-test',
  files('../tests/terrain_cull_test.c') + core_sources,
  include_directories: [core_inc],
  dependencies: [cglm_dep, math_dep],
  install: false,
)
test('terrain-cull', terrain_cull_test)
