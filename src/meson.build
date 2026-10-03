# Core and backend sources compile together, but the directories are the hard
# boundary: core only talks to the RenderAPI (render.h) and the input contract
# (input.h); each backend owns its platform API (GL/GLFW or native KOS PVR).

opt_platform = get_option('platform')

core_inc = include_directories('core')
core_sources = files(
    'core/camera.c',
    'core/car.c',
    'core/chase_cam.c',
    'core/game.c',
    'core/input.c',
    'core/mesh.c',
    'core/render.c',
    'core/terrain.c',
    'core/texture.c',
)

if opt_platform == 'desktop'
    backend_inc = include_directories('backend/desktop')
    backend_sources = files(
        'backend/desktop/main.c',
        'backend/desktop/input_glfw.c',
        'backend/desktop/render_gl.c',
        'backend/desktop/gfx_gl.c',
        'backend/desktop/texture_gl.c',
    )
    platform_deps = [glad_dep, glfw_dep]
    platform_args = ['-DGLFW_INCLUDE_NONE']
    platform_inc = []
    exe_name = 'island'
elif opt_platform == 'dreamcast'
    # Native KOS PVR backend. KOS headers come from the toolchain install;
    # the desktop backend dir is on the include path only for the shared
    # stb_image.h decoder.
    backend_inc = include_directories('backend/dreamcast')
    backend_sources = files(
        'backend/dreamcast/main_dc.c',
        'backend/dreamcast/input_dc.c',
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
    exe_name = 'island.elf'
else
    error('Unsupported platform: ' + opt_platform)
endif

island_sources = core_sources + backend_sources

# Art assets: <source root>/assets is copied next to the built binary so the
# game can load textures at runtime (<build root>/src/assets, binary at
# <build root>/src/island). Regenerate textures with
# `python3 tools/make_textures.py [seed]` (palette: docs/art-direction.md).
assets_src_dir = meson.project_source_root() / 'assets'
assets_dst_parent = meson.project_build_root() / 'src'

copy_assets = custom_target('copy-assets',
    input: [],
    output: 'assets.stamp',
    command: [find_program('sh'), '-c',
        'rm -rf "$2/assets" && cp -R "$1" "$2" && touch "$0"',
        '@OUTPUT@', assets_src_dir, assets_dst_parent],
)

# copy_assets (a .stamp) in the source list forces meson to build the asset
# copy before the binary, without trying to compile the stamp itself.
executable(exe_name,
    island_sources + [copy_assets],
    include_directories: [core_inc, backend_inc, platform_inc],
    dependencies: platform_deps + [cglm_dep, math_dep],
    c_args: platform_args,
    install: false)

# GL-free smoke test: core must record a full frame with no backend linked at
# all. This is the "recording stub backend" the RenderAPI task asks for.
# native: true keeps the tests running on the host during cross builds.
api_smoke = executable('api-smoke',
    files('../tests/api_smoke.c') + core_sources,
    include_directories: [core_inc],
    dependencies: [cglm_dep, math_dep],
    native: true,
    install: false)
test('api-smoke', api_smoke)

# GL-free test for the player mode state machine (on-foot/in-vehicle).
mode_test = executable('mode-test',
    files('../tests/mode_test.c') + core_sources,
    include_directories: [core_inc],
    dependencies: [cglm_dep, math_dep],
    native: true,
    install: false)
test('mode', mode_test)

# GL-free test for the MVP delivery loop (island1-pkcj).
quest_test = executable('quest-test',
    files('../tests/quest_test.c') + core_sources,
    include_directories: [core_inc],
    dependencies: [cglm_dep, math_dep],
    native: true,
    install: false)
test('quest', quest_test)

# GL-free test for the platform-agnostic input contract (actions/axes/edges).
input_test = executable('input-test',
    files('../tests/input_test.c', 'core/input.c'),
    include_directories: [core_inc],
    dependencies: [math_dep],
    native: true,
    install: false)
test('input', input_test)

# GL-free behaviour test for the chase camera spring/lag.
chase_cam_test = executable('chase-cam-test',
    files('../tests/chase_cam_test.c', 'core/chase_cam.c'),
    include_directories: [core_inc],
    dependencies: [cglm_dep, math_dep],
    native: true,
    install: false)
test('chase-cam', chase_cam_test)

# GL-free behaviour test for the hover-car physics (island1-9n0n).
car_test = executable('car-test',
    files('../tests/car_test.c', 'core/car.c'),
    include_directories: [core_inc],
    dependencies: [cglm_dep, math_dep],
    native: true,
    install: false)
test('car', car_test)
