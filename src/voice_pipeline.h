#ifndef VOICE_PIPELINE_H
#define VOICE_PIPELINE_H

#include <xcore/parallel.h>
#include <xcore/channel.h>
#include <xcore/chanend.h>
#include "voice.h"
// #include "xua_conf.h"

#define VOICE_PIPELINE_FRAME_ADVANCE    (AEC_FRAME_ADVANCE)
#define VOICE_PIPELINE_NUM_CHANNEL      (AEC_MAX_Y_CHANNELS+AEC_MAX_X_CHANNELS)
#define VOICE_PIPELINE_MIC_INDEX        (XUA_PDM_MIC_INDEX)

#define VOICE_PIPELINE_EXCHANGE_NUM_CHANNEL     (VOICE_PIPELINE_NUM_CHANNEL+2)

#define VOICE_PIPELINE_MIC_DELAY_MS             (0)
#define VOICE_PIPELINE_FIXED_DELAY_SAMPLES      (16000 * VOICE_PIPELINE_MIC_DELAY_MS / 1000)

typedef struct {
    chanend_t c_voice;
    int32_t data_in[VOICE_PIPELINE_EXCHANGE_NUM_CHANNEL][VOICE_PIPELINE_FRAME_ADVANCE];
    int32_t data_out[VOICE_PIPELINE_EXCHANGE_NUM_CHANNEL][VOICE_PIPELINE_FRAME_ADVANCE];
    int32_t frame_idx;
} voice_pipeline_data_exchange_handle_t;

void voice_pipeline_data_exchange_init(voice_pipeline_data_exchange_handle_t* handle, chanend_t c_voice);
/**
 * data_in: int32_t[VOICE_PIPELINE_EXCHANGE_NUM_CHANNEL], ref0 ref1 mic0 mic1 proc0 proc1
 */
void voice_pipeline_data_exchange(voice_pipeline_data_exchange_handle_t* handle, int32_t* data_in, int32_t* data_out);

DECLARE_JOB(voice_pipeline, (chanend_t))
void voice_pipeline(chanend_t c_voice);

#endif