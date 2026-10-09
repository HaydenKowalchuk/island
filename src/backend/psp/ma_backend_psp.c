#include <math.h>
#include <miniaudio.h>
#include <stdio.h>
#include <string.h>

#if defined(__psp__) || defined(PSP)
#define MA_PSP
#endif

#if defined(MA_PSP)
#include <pspaudio.h>
#include <pspthreadman.h>
#include <psputils.h>

#define PSP_AUDIO_SRC_MIN_PERIOD 17
#define PSP_AUDIO_SRC_MAX_PERIOD 4111

typedef struct ma_context_state_psp {
  int _unused;
} ma_context_state_psp;

typedef struct ma_device_state_psp {
  ma_device* pDevice;
  ma_uint32 periodSizeInBytes;
  ma_uint32 periodSizeInFrames;
  void* pBufferRaw;
  void* pBuffers[2];
  int    readIndex;
  int    writeIndex;
  volatile int running;
  SceUID threadId;
  SceUID slotSema;
  SceUID bufSema;
} ma_device_state_psp;

static ma_device_state_psp* g_psp_audio_state;

static ma_context_state_psp* ma_context_get_backend_state__psp(ma_context* pContext) {
  return (ma_context_state_psp*)ma_context_get_backend_state(pContext);
}

static ma_device_state_psp* ma_device_get_backend_state__psp(ma_device* pDevice) {
  return (ma_device_state_psp*)ma_device_get_backend_state(pDevice);
}

static void ma_backend_info__psp(ma_device_backend_info* pBackendInfo) {
  pBackendInfo->pName = "PlayStation Portable";
}

static ma_result ma_context_init__psp(ma_context* pContext, const void* pContextBackendConfig, void** ppContextState) {
  ma_context_state_psp* pContextStatePsp =
      (ma_context_state_psp*)ma_calloc(sizeof(*pContextStatePsp),
                                       ma_context_get_allocation_callbacks(pContext));
  if (pContextStatePsp == NULL) return MA_OUT_OF_MEMORY;
  (void)pContextBackendConfig;
  *ppContextState = pContextStatePsp;
  return MA_SUCCESS;
}

static void ma_context_uninit__psp(ma_context* pContext) {
  ma_context_state_psp* pContextStatePsp = ma_context_get_backend_state__psp(pContext);
  ma_free(pContextStatePsp, ma_context_get_allocation_callbacks(pContext));
}

static ma_result ma_context_enumerate_devices__psp(ma_context* pContext,
                                                   ma_enum_devices_callback_proc callback,
                                                   void* pCallbackUserData) {
  ma_device_info deviceInfo;
  (void)pContext;
  memset(&deviceInfo, 0, sizeof(deviceInfo));
  deviceInfo.isDefault = MA_TRUE;
  deviceInfo.id.custom.i = 0;
  ma_strncpy_s(deviceInfo.name, sizeof(deviceInfo.name), "Default Playback Device", (size_t)-1);
  ma_device_info_add_native_data_format(&deviceInfo, ma_format_s16, 2, 2, 44100, 44100);
  if (callback(ma_device_type_playback, &deviceInfo, pCallbackUserData) == MA_DEVICE_ENUMERATION_ABORT)
    return MA_SUCCESS;
  return MA_SUCCESS;
}

static int audio_thread_func(SceSize args, void* argp) {
  (void)args;
  (void)argp;
  ma_device_state_psp* pState = g_psp_audio_state;
  if (!pState) return 0;

  int readIdx = 0;
  while (pState->running) {
    sceKernelWaitSema(pState->bufSema, 1, NULL);
    if (!pState->running) break;

    sceAudioSRCOutputBlocking(PSP_AUDIO_VOLUME_MAX, pState->pBuffers[readIdx]);
    sceKernelSignalSema(pState->slotSema, 1);
    readIdx = (readIdx + 1) & 1;
  }
  return 0;
}

static ma_result ma_device_init__psp(ma_device* pDevice, const void* pDeviceBackendConfig, ma_device_descriptor* pDescriptorPlayback, ma_device_descriptor* pDescriptorCapture, void** ppDeviceState) {
  ma_device_state_psp* pDeviceStatePsp;
  ma_log* pLog = ma_device_get_log(pDevice);
  ma_uint32 periodSizeInFrames, periodSizeInBytes;

  (void)pDeviceBackendConfig;
  (void)pDescriptorCapture;
  if (ma_device_get_type(pDevice) != ma_device_type_playback)
    return MA_DEVICE_TYPE_NOT_SUPPORTED;

  while (sceAudioOutput2GetRestSample() > 0)
    sceKernelDelayThread(1000);
  sceAudioSRCChRelease();

  periodSizeInFrames = ma_calculate_buffer_size_in_frames_from_descriptor(pDescriptorPlayback, 44100);
  periodSizeInFrames = ma_clamp(periodSizeInFrames, PSP_AUDIO_SRC_MIN_PERIOD, PSP_AUDIO_SRC_MAX_PERIOD);
  periodSizeInBytes = periodSizeInFrames * ma_get_bytes_per_frame(ma_format_s16, 2);
  fprintf(stdout, "[audio] pdev init: period=%u frames, %u bytes\n", periodSizeInFrames, periodSizeInBytes);

  fprintf(stdout, "[audio] Reserving SRC: samples=%u freq=44100\n", (unsigned)periodSizeInFrames);
  if (sceAudioSRCChReserve((int)periodSizeInFrames, 44100, 2) < 0) {
    ma_log_postf(pLog, MA_LOG_LEVEL_ERROR, "[PSP] sceAudioSRCChReserve failed.");
    return MA_ERROR;
  }

  pDeviceStatePsp = (ma_device_state_psp*)ma_calloc(sizeof(*pDeviceStatePsp),
                                                    ma_device_get_allocation_callbacks(pDevice));
  if (pDeviceStatePsp == NULL) {
    sceAudioSRCChRelease();
    return MA_OUT_OF_MEMORY;
  }

  pDeviceStatePsp->pDevice            = pDevice;
  pDeviceStatePsp->periodSizeInBytes  = periodSizeInBytes;
  pDeviceStatePsp->periodSizeInFrames = periodSizeInFrames;
  pDeviceStatePsp->readIndex          = 0;
  pDeviceStatePsp->writeIndex         = 0;
  pDeviceStatePsp->running            = 0;
  pDeviceStatePsp->threadId           = 0;

  size_t totalAlloc = (size_t)periodSizeInBytes * 2 + 64;
  pDeviceStatePsp->pBufferRaw = ma_malloc(totalAlloc, ma_device_get_allocation_callbacks(pDevice));
  if (pDeviceStatePsp->pBufferRaw == NULL) {
    sceAudioSRCChRelease();
    ma_free(pDeviceStatePsp, ma_device_get_allocation_callbacks(pDevice));
    return MA_OUT_OF_MEMORY;
  }
  uintptr_t addr = (uintptr_t)pDeviceStatePsp->pBufferRaw;
  uintptr_t aligned = (addr + 63) & ~(uintptr_t)63;
  pDeviceStatePsp->pBuffers[0] = (void*)aligned;
  pDeviceStatePsp->pBuffers[1] = (void*)(aligned + periodSizeInBytes);

  pDeviceStatePsp->slotSema = sceKernelCreateSema("audio_slot", 0, 2, 2, NULL);
  pDeviceStatePsp->bufSema  = sceKernelCreateSema("audio_buf",  0, 0, 2, NULL);

  g_psp_audio_state = pDeviceStatePsp;

  pDeviceStatePsp->running = 1;
  pDeviceStatePsp->threadId = sceKernelCreateThread("audio_thread", audio_thread_func,
      0x10, 0x4000, PSP_THREAD_ATTR_USER, 0);
  if (pDeviceStatePsp->threadId < 0) {
    pDeviceStatePsp->running = 0;
    g_psp_audio_state = NULL;
    sceKernelDeleteSema(pDeviceStatePsp->slotSema);
    sceKernelDeleteSema(pDeviceStatePsp->bufSema);
    sceAudioSRCChRelease();
    ma_free(pDeviceStatePsp->pBufferRaw, ma_device_get_allocation_callbacks(pDevice));
    ma_free(pDeviceStatePsp, ma_device_get_allocation_callbacks(pDevice));
    return MA_ERROR;
  }
  sceKernelStartThread(pDeviceStatePsp->threadId, 0, NULL);

  *ppDeviceState = pDeviceStatePsp;

  pDescriptorPlayback->format = ma_format_s16;
  pDescriptorPlayback->channels = 2;
  pDescriptorPlayback->sampleRate = 44100;
  pDescriptorPlayback->periodSizeInFrames = periodSizeInFrames;
  pDescriptorPlayback->periodCount = 2;
  fprintf(stdout, "[audio] backend set desc ps=%u fmt=%d ch=%u sr=%u\n", pDescriptorPlayback->periodSizeInFrames, pDescriptorPlayback->format, pDescriptorPlayback->channels, pDescriptorPlayback->sampleRate);
  return MA_SUCCESS;
}

static void ma_device_uninit__psp(ma_device* pDevice) {
  ma_device_state_psp* pDeviceStatePsp = ma_device_get_backend_state__psp(pDevice);

  if (pDeviceStatePsp->running) {
    pDeviceStatePsp->running = 0;
    sceKernelSignalSema(pDeviceStatePsp->bufSema, 1);
    sceKernelWaitThreadEnd(pDeviceStatePsp->threadId, NULL);
    sceKernelDeleteThread(pDeviceStatePsp->threadId);
  }

  sceKernelDeleteSema(pDeviceStatePsp->slotSema);
  sceKernelDeleteSema(pDeviceStatePsp->bufSema);

  while (sceAudioOutput2GetRestSample() > 0)
    sceKernelDelayThread(1000);
  sceAudioSRCChRelease();
  ma_free(pDeviceStatePsp->pBufferRaw, ma_device_get_allocation_callbacks(pDevice));
  ma_free(pDeviceStatePsp, ma_device_get_allocation_callbacks(pDevice));
}

static ma_result ma_device_start__psp(ma_device* pDevice) {
  ma_device_state_psp* pDeviceStatePsp = ma_device_get_backend_state__psp(pDevice);

  pDeviceStatePsp->readIndex  = 0;
  pDeviceStatePsp->writeIndex = 0;

  return MA_SUCCESS;
}

static ma_result ma_device_stop__psp(ma_device* pDevice) {
  (void)pDevice;
  return MA_SUCCESS;
}

static ma_result ma_device_step__psp(ma_device* pDevice, ma_blocking_mode blockingMode) {
  (void)blockingMode;
  ma_device_state_psp* pDeviceStatePsp = ma_device_get_backend_state__psp(pDevice);

  if (sceKernelPollSema(pDeviceStatePsp->slotSema, 1) == 0) {
    int idx = pDeviceStatePsp->writeIndex;
    ma_device_handle_backend_data_callback(pDevice, pDeviceStatePsp->pBuffers[idx], NULL, pDeviceStatePsp->periodSizeInFrames);
    sceKernelDcacheWritebackInvalidateRange(pDeviceStatePsp->pBuffers[idx], pDeviceStatePsp->periodSizeInBytes);
    pDeviceStatePsp->writeIndex = (idx + 1) & 1;
    sceKernelSignalSema(pDeviceStatePsp->bufSema, 1);
  }

  return MA_SUCCESS;
}

static void ma_device_wakeup__psp(ma_device* pDevice) {
  (void)pDevice;
}

static ma_device_backend_vtable ma_gDeviceBackendVTable_Psp =
    {
        ma_backend_info__psp,
        ma_context_init__psp,
        ma_context_uninit__psp,
        ma_context_enumerate_devices__psp,
        ma_device_init__psp,
        ma_device_uninit__psp,
        ma_device_start__psp,
        ma_device_stop__psp,
        ma_device_step__psp,
        ma_device_wakeup__psp};

ma_device_backend_vtable* ma_device_backend_psp = &ma_gDeviceBackendVTable_Psp;

MA_API ma_device_backend_vtable* ma_psp_get_vtable(void) {
  return ma_device_backend_psp;
}

MA_API ma_context_config_psp ma_context_config_psp_init(void) {
  ma_context_config_psp config;
  memset(&config, 0, sizeof(config));
  return config;
}

MA_API ma_device_config_psp ma_device_config_psp_init(void) {
  ma_device_config_psp config;
  memset(&config, 0, sizeof(config));
  return config;
}

#else /* !MA_PSP */

ma_device_backend_vtable* ma_device_backend_psp = NULL;

MA_API ma_device_backend_vtable* ma_psp_get_vtable(void) {
  return NULL;
}

MA_API ma_context_config_psp ma_context_config_psp_init(void) {
  ma_context_config_psp config;
  memset(&config, 0, sizeof(config));
  return config;
}

MA_API ma_device_config_psp ma_device_config_psp_init(void) {
  ma_device_config_psp config;
  memset(&config, 0, sizeof(config));
  return config;
}

#endif /* MA_PSP */
