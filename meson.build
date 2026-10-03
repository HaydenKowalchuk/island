project('island', 'c',
    version: '0.1.0',
    default_options: [
        'c_std=c99',
        'warning_level=2',
        'buildtype=debugoptimized',
        'default_library=static',
        'b_staticpic=false',
    ])

cc = meson.get_compiler('c')

platform = get_option('platform')
if platform == 'desktop' and meson.is_cross_build()
    error('platform=desktop cannot be used with a cross file')
endif

math_dep = cc.find_library('m', required: false)

# Math for view/projection matrices (header-only, used by every backend).
cglm_dep = dependency('cglm', required: false)
if not cglm_dep.found()
    # Header-only, but its fallback also ships a static lib; keep it non-PIC so
    # the PSP (MIPS eabi) linker stays happy.
    cglm_proj = subproject('cglm', default_options: ['default_library=static'])
    cglm_dep = cglm_proj.get_variable('cglm_dep')
endif

# Desktop-only windowing/input/GL loader. The PSP backend talks to the GE
# directly (sceGu), so it needs none of this.
glfw_dep = disabler()
glad_dep = disabler()
if platform == 'desktop'
    # Prefer a system GLFW, fall back to the bundled wrap.
    glfw_dep = dependency('glfw3', required: false)
    if not glfw_dep.found()
        glfw_dep = subproject('glfw').get_variable('glfw_dep')
    endif

    # OpenGL 1.2/1.5 fixed-function loader (gl=2.1 compatibility profile).
    glad_dep = subproject('glad').get_variable('glad_dep')
endif

subdir('src')

# Layering guard: src/core must stay platform-agnostic (see the task's rules).
if platform == 'desktop'
    bash = find_program('bash')
    test('layering', bash, args: [files('scripts/check_layering.sh')])
endif
