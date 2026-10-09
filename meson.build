project(
  'island',
  'c',
  version: '0.1.0',
  default_options: [
    'c_std=gnu17',
    'warning_level=2',
    'buildtype=debugoptimized',
    'default_library=static',
    'b_staticpic=false',
  ],
)

cc = meson.get_compiler('c')
opt_platform = get_option('platform')

# Dependencies.
#
# Desktop: GLFW windowing plus GLAD (OpenGL 1.2/1.5 fixed-function loader,
# gl=2.1 compatibility profile) as wrap subprojects.
#
# Dreamcast: native KOS PVR (Tile Accelerator) — no GL and no GLdc; the KOS
# toolchain provides everything. Cross-build with:
#   meson setup build-dc --cross-file sh4-dreamcast-kos -Dplatform=dreamcast
glad_dep = declare_dependency()
glfw_dep = declare_dependency()

if opt_platform == 'desktop'
  # Windowing / input. Prefer a system GLFW, fall back to the bundled wrap.
  glfw_dep = dependency('glfw3', required: false)
  if not glfw_dep.found()
    glfw_proj = subproject('glfw')
    glfw_dep = glfw_proj.get_variable('glfw_dep')
  endif

  glad_dep = subproject('glad').get_variable('glad_dep')
elif opt_platform == 'dreamcast'
  # SH-4 has no double-precision FPU: keep all math single-precision and
  # help branchless codegen (known-good KOS toolchain flags).
  add_project_arguments('-fsingle-precision-constant', '-mpretend-cmove', language: 'c')
elif opt_platform == 'psp'
else
  error('Unsupported platform: ' + opt_platform + ' (desktop, psp or dreamcast)')
endif

# Math for view/projection matrices (header-only, used by every backend).
cglm_dep = dependency('cglm', required: false)
if not cglm_dep.found()
  # Header-only, but its fallback also ships a static lib; keep it non-PIC so
  # the PSP (MIPS eabi) linker stays happy.
  cglm_proj = subproject('cglm', default_options: ['default_library=static'])
  cglm_dep = cglm_proj.get_variable('cglm_dep')
endif

# miniaudio: single-header audio (decoders + device backends). Vendored as a
# slimmed snapshot (just meson.build + miniaudio.c + miniaudio.h) matching
# integrity's known-good build on desktop/dreamcast/psp -- dev-0.12 branch,
# commit a55d2e7e, already containing the MA_PSP stub plumbing and the full
# native MA_SUPPORT_DREAMCAST (KOS/AICA) backend inlined. Not a wrap-file on
# purpose: island1-26pd will edit this header directly to flesh out the real
# PSP device backend, and a wrap would get clobbered/re-downloaded on top of
# those local edits.
ma_proj = subproject('miniaudio')
miniaudio_dep = ma_proj.get_variable('ma_dep')

math_dep = cc.find_library('m', required: false)

subdir('src')

# Layering guard: src/core must stay platform-agnostic (see the task's rules).
if opt_platform == 'desktop'
  bash = find_program('bash')
  test('layering', bash, args: [files('scripts/check_layering.sh')])
endif
