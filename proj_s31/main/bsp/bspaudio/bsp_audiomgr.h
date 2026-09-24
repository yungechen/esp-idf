#ifndef __BSP_AUDIOMGR_H__
#define __BSP_AUDIOMGR_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include "format_wav.h"
esp_err_t bsp_audiomgr_init(void);

esp_err_t audio_mic_start(int duration_sec);

#ifdef __cplusplus
}
#endif
#endif