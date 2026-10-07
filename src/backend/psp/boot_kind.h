#ifndef ISLAND_BACKEND_PSP_BOOT_KIND_H
#define ISLAND_BACKEND_PSP_BOOT_KIND_H

/*
 * How the game was launched, inferred from argv[0]/cwd in main.c's
 * boot_report(). The kernel launch-info APIs (sceKernelBootFrom /
 * sceKernelInitApitype / sceKernelInitFileName) return 0x8002013A in every
 * tested environment — emulator, debugger and a real disc boot — so they are
 * not consulted. Set once before game_init; texture_gu branches on it to pick
 * absolute sceIo reads (disc/iso) vs relative paths (everything else).
 */
enum boot_kind {
  BOOT_UNKNOWN,  /* no recognisable device prefix */
  BOOT_HOST,     /* host0: — debugger host fs (dev tree) */
  BOOT_DISC,     /* disc0:/umd0: — UMD or ISO game boot */
  BOOT_MEMSTICK, /* ms0:/ef0: — EBOOT.PBP from the stick */
};

enum boot_kind psp_boot_kind(void);
void psp_set_boot_kind(enum boot_kind kind);

#endif
