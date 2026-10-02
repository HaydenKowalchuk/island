project('island', 'c',
    version: '0.1.0',
    default_options: [
        'c_std=c99',
        'warning_level=2',
        'buildtype=debugoptimized',
    ])

cc = meson.get_compiler('c')

add_project_arguments(
    '-DGLFW_INCLUDE_NONE',
    language: 'c',
)

# Windowing / input. Prefer a system GLFW, fall back to the bundled wrap.
glfw_dep = dependency('glfw3', required: false)
if not glfw_dep.found()
    glfw_proj = subproject('glfw')
    glfw_dep = glfw_proj.get_variable('glfw_dep')
endif

# OpenGL 1.2/1.5 fixed-function loader (gl=2.1 compatibility profile).
glad_dep = subproject('glad').get_variable('glad_dep')

# Math for view/projection matrices.
cglm_dep = dependency('cglm', required: false)
if not cglm_dep.found()
    cglm_dep = subproject('cglm').get_variable('cglm_dep')
endif

math_dep = cc.find_library('m', required: false)

core_deps = [glad_dep, glfw_dep, cglm_dep, math_dep]

subdir('src')