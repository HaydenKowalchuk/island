project('miniaudio', 'c')

ma_inc = include_directories('.')
ma_c_args = []
ma_link_args = []
ma_deps = []

# PSP ('none') and Dreamcast ('dreamcast') have minimal POSIX implementations.
if host_machine.system() == 'psp'
  ma_c_args += ['-include', 'sys/select.h', '-DMA_NO_RUNTIME_LINKING']
elif host_machine.system() == 'dreamcast'
  ma_link_args += ['-lpthread']
  #threads_dep = dependency('threads', static: true, required: true)
  #ma_deps += threads_dep
  ma_c_args += ['-include', 'sys/select.h', '-DMA_NO_RUNTIME_LINKING']
endif

ma_lib = static_library(
  'miniaudio',
  'miniaudio.c',
  include_directories: ma_inc,
  c_args: ma_c_args,
  pic: false,
  link_args: ma_link_args,
)

ma_dep = declare_dependency(
  include_directories: ma_inc,
  dependencies: ma_deps,
  compile_args: ma_c_args,
  link_with: ma_lib,
)
