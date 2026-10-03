# Two backends share the same platform-agnostic core. The directories are the
# hard boundary: core only talks to the RenderAPI (render.h) and the input
# contract (input.h); each backend owns its platform's window/loop/renderer.
#
#   desktop -> GLFW + OpenGL 2.1 compat (glad)
#   psp     -> pspsdk + sceGu/sceGum (native GE), packaged to EBOOT.PBP

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

if platform == 'psp'

    backend_inc = include_directories('backend/psp', 'backend/common')

    backend_sources = files(
        'backend/psp/main.c',
        'backend/psp/render_gu.c',
        'backend/psp/texture_gu.c',
        'backend/psp/input_psp.c',
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
        '-Wl,-q,-T/opt/toolchains/pspdev/psp/sdk/lib/linkfile.prx',
        '/opt/toolchains/pspdev/psp/sdk/lib/prxexports.o',
    ]

    island_exe = executable('island',
        core_sources + backend_sources,
        include_directories: [core_inc, backend_inc],
        dependencies: [cglm_dep],
        link_args: psp_link_args,
        install: false)

    # Keep the art next to EBOOT.PBP. The loader also searches host0:/assets and
    # ms0:/PSP/GAME/island/assets, so putting it here is just convenient.
    custom_target('psp-copy-assets',
        input: [],
        output: 'assets.stamp',
        command: [find_program('sh'), '-c',
            'rm -rf "$2/assets" && cp -R "$1" "$2" && touch "$0"',
            '@OUTPUT@', meson.project_source_root() / 'assets',
            meson.current_build_dir()],
        build_by_default: true)

    # Post-process into a homebrew EBOOT.PBP (title, prx, then the two CFW
    # launcher aliases). Mirrors the working recipe in the dc/platformer tree.
    mksfo = find_program('mksfo', required: true)
    psp_fixup = find_program('psp-fixup-imports', required: true)
    psp_prxgen = find_program('psp-prxgen', required: true)
    pack_pbp = find_program('pack-pbp', required: true)

    sfo = custom_target('island_sfo',
        output: 'param.sfo',
        command: [mksfo, 'island', '@OUTPUT@'])

    custom_target('island_psp_pbp',
        input: [island_exe, sfo],
        output: 'EBOOT.PBP',
        command: [
            find_program('sh'), '-c',
            'd=$(dirname "$2"); '
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
        install: false)

else

    backend_inc = include_directories('backend/desktop', 'backend/common')

    backend_sources = files(
        'backend/desktop/main.c',
        'backend/desktop/input_glfw.c',
        'backend/desktop/render_gl.c',
        'backend/desktop/gfx_gl.c',
        'backend/desktop/texture_gl.c',
    )

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
    executable('island',
        island_sources + [copy_assets],
        include_directories: [core_inc, backend_inc],
        dependencies: [glad_dep, glfw_dep, cglm_dep, math_dep],
        c_args: ['-DGLFW_INCLUDE_NONE'],
        install: false)

    # GL-free smoke test: core must record a full frame with no backend linked at
    # all. This is the "recording stub backend" the RenderAPI task asks for.
    api_smoke = executable('api-smoke',
        files('../tests/api_smoke.c') + core_sources,
        include_directories: [core_inc],
        dependencies: [cglm_dep, math_dep],
        install: false)
    test('api-smoke', api_smoke)

    # GL-free test for the player mode state machine (on-foot/in-vehicle).
    mode_test = executable('mode-test',
        files('../tests/mode_test.c') + core_sources,
        include_directories: [core_inc],
        dependencies: [cglm_dep, math_dep],
        install: false)
    test('mode', mode_test)

    # GL-free test for the MVP delivery loop (island1-pkcj).
    quest_test = executable('quest-test',
        files('../tests/quest_test.c') + core_sources,
        include_directories: [core_inc],
        dependencies: [cglm_dep, math_dep],
        install: false)
    test('quest', quest_test)

    # GL-free test for the platform-agnostic input contract (actions/axes/edges).
    input_test = executable('input-test',
        files('../tests/input_test.c', 'core/input.c'),
        include_directories: [core_inc],
        dependencies: [math_dep],
        install: false)
    test('input', input_test)

    # GL-free behaviour test for the chase camera spring/lag.
    chase_cam_test = executable('chase-cam-test',
        files('../tests/chase_cam_test.c', 'core/chase_cam.c'),
        include_directories: [core_inc],
        dependencies: [cglm_dep, math_dep],
        install: false)
    test('chase-cam', chase_cam_test)

    # GL-free behaviour test for the hover-car physics (island1-9n0n).
    car_test = executable('car-test',
        files('../tests/car_test.c', 'core/car.c'),
        include_directories: [core_inc],
        dependencies: [cglm_dep, math_dep],
        install: false)
    test('car', car_test)

endif
