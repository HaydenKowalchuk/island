# Single desktop target. Core and backend sources compile together, but the
# directories are the hard boundary: core only talks to the RenderAPI (render.h)
# and the input contract (input.h); the desktop backend owns GL/GLFW.

core_inc = include_directories('core')
backend_inc = include_directories('backend/desktop')

island_sources = files(
    'core/camera.c',
    'core/game.c',
    'core/input.c',
    'core/mesh.c',
    'core/render.c',
    'backend/desktop/main.c',
    'backend/desktop/input_glfw.c',
    'backend/desktop/render_gl.c',
    'backend/desktop/gfx_gl.c',
)

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
