#ifndef ISLAND_BACKEND_PSP_PSP_SETUP_H
#define ISLAND_BACKEND_PSP_PSP_SETUP_H

/*
 * PSP module boilerplate, included by exactly one translation unit (main.c).
 * Everything is `static` so an accidental double include cannot clash; the
 * module-info macros must appear once at file scope.
 */

#include <pspkernel.h>

PSP_MODULE_INFO("island", 0, 1, 0);

/* Give newlib the heap: all but ~1 MB, leaving room for stacks/VRAM. */
PSP_HEAP_SIZE_KB(-1024);

PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU);
PSP_DISABLE_NEWLIB_TIMEZONE_SUPPORT();

static volatile int psp_done = 0;

static int psp_exit_callback(int arg1, int arg2, void* common) {
    (void)arg1;
    (void)arg2;
    (void)common;
    psp_done = 1;
    return 0;
}

static int psp_callback_thread(SceSize args, void* argp) {
    (void)args;
    (void)argp;
    int cbid = sceKernelCreateCallback("island exit", psp_exit_callback, NULL);
    if (cbid >= 0)
        sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();
    return 0;
}

/* Home / power events set `psp_done`, so the main loop can exit cleanly. */
static int psp_setup_callbacks(void) {
    int thid = sceKernelCreateThread("island update", psp_callback_thread,
                                     0x11, 0xFA0, 0, 0);
    if (thid >= 0)
        sceKernelStartThread(thid, 0, 0);
    return thid;
}

#endif
