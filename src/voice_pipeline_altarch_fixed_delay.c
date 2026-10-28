#include "voice_pipeline.h"

#include <xcore/select.h>
#include <xcore/channel_transaction.h>
#include <xcore/interrupt.h>
#include <xcore/interrupt_wrappers.h>
#include <xcore/triggerable.h>

#define VOICE_PIPELINE_FRAME_POOL_SIZE  (3)

typedef struct {
    // Stage1 - AEC, DE, ADEC
    stage1_t DWORD_ALIGNED stage_1_state;
    // IC, VNR
    ic_state_t DWORD_ALIGNED ic_state;
    // NS
    ns_state_t DWORD_ALIGNED ns_state;
    // AGC
    agc_state_t agc_state;
} voice_pipeline_states_t;

typedef struct {
    float_s32_t max_ref_energy;
    float_s32_t aec_corr_factor[AEC_MAX_Y_CHANNELS];
    int32_t ref_active_flag;
    float_s32_t vnr_pred_flag;
} voice_pipeline_metadata_t;

typedef struct {
    int32_t DWORD_ALIGNED ref[AEC_MAX_X_CHANNELS][VOICE_PIPELINE_FRAME_ADVANCE];
    int32_t DWORD_ALIGNED mic[AEC_MAX_Y_CHANNELS][VOICE_PIPELINE_FRAME_ADVANCE];
    int32_t DWORD_ALIGNED proc[2][VOICE_PIPELINE_FRAME_ADVANCE];
    voice_pipeline_metadata_t metadata;
} voice_pipeline_frame_t;

typedef struct {
    chanend_t c_voice;
    chanend_t c_buf_idx;
    voice_pipeline_frame_t frame_pool[VOICE_PIPELINE_FRAME_POOL_SIZE];
    size_t in_pool_idx;
    size_t out_pool_idx;
    size_t frame_idx;
} voice_pipeline_handle_t;

extern void stage1_fixed_delay_alt_arch_process_frame(
    stage1_t *state, int32_t (*output_frame)[AEC_FRAME_ADVANCE],
    float_s32_t *max_ref_energy, float_s32_t *aec_corr_factor, int32_t *ref_active_flag,
    int32_t (*input_y)[AEC_FRAME_ADVANCE], int32_t (*input_x)[AEC_FRAME_ADVANCE]);

void stage_stage1_init(voice_pipeline_states_t* states) {
    // Initialise AEC Fixed Delay ALT ARCH
    aec_conf_t aec_de_mode_conf, aec_non_de_mode_conf;

    aec_non_de_mode_conf.num_y_channels = 1;
    aec_non_de_mode_conf.num_x_channels = 2;
    aec_non_de_mode_conf.num_main_filt_phases = 15;
    aec_non_de_mode_conf.num_shadow_filt_phases = 5;
    aec_non_de_mode_conf.tdist = &aec_tdist_chans2_threads1;

    aec_de_mode_conf.num_y_channels = 1;
    aec_de_mode_conf.num_x_channels = 1;
    aec_de_mode_conf.num_main_filt_phases = 30;
    aec_de_mode_conf.num_shadow_filt_phases = 0;
    aec_de_mode_conf.tdist = &aec_tdist_chans2_threads1;

    // ADEC will not run in actual stage; these fields are unused but left inert
    adec_config_t adec_conf;
    adec_conf.bypass = 1;
    adec_conf.force_de_cycle_trigger = 0;
    stage1_init(&states->stage_1_state, &aec_de_mode_conf, &aec_non_de_mode_conf, &adec_conf);

    // Apply the fixed delay directly; without ADEC the delay buffer is never retimed
    update_delay_samples(&states->stage_1_state.delay_state, VOICE_PIPELINE_FIXED_DELAY_SAMPLES);
}

void stage_vnr_ic_init(voice_pipeline_states_t *state) {
    memset(&state->ic_state, 0, sizeof(ic_state_t));
    // Initialise IC, VNR
    ic_init(&state->ic_state);
}

void stage_ns_init(voice_pipeline_states_t *state) {
    memset(&state->ns_state, 0, sizeof(ns_state_t));
    ns_init(&state->ns_state);
}

void stage_agc_init(voice_pipeline_states_t *state) {
    memset(&state->agc_state, 0, sizeof(agc_state_t));
    agc_config_t agc_conf_asr = AGC_PROFILE_ASR;
    agc_init(&state->agc_state, &agc_conf_asr);
}

void stage_bypass(voice_pipeline_states_t *states, voice_pipeline_frame_t *frame) {
    /** Bypass stage - simply copy input to output */
    memcpy(&frame->proc[0][0], &frame->mic[0][0], 2 * VOICE_PIPELINE_FRAME_ADVANCE * sizeof(int32_t));
}

void stage_stage1(voice_pipeline_states_t *states, voice_pipeline_frame_t *frame) {
    /** Stage1 - AEC, DE, ADEC*/
    // stage1 will not process the frame in-place,
    // since mic input is needed to overwrite the output in certain cases

    stage1_fixed_delay_alt_arch_process_frame(&states->stage_1_state, frame->proc, &frame->metadata.max_ref_energy,
            &frame->metadata.aec_corr_factor[0], &frame->metadata.ref_active_flag, frame->mic, frame->ref);
}

void stage_vnr_ic(voice_pipeline_states_t *states, voice_pipeline_frame_t *frame) {
    /** IC and VNR*/
    int32_t ic_output[VOICE_PIPELINE_FRAME_ADVANCE];
    float_s32_t input_vnr_pred;

    if (frame->metadata.ref_active_flag) {
        states->ic_state.config_params.bypass = 1;
    } else {
        states->ic_state.config_params.bypass = 0;
    }

    ic_process_frame(
        &states->ic_state, 
        frame->proc[0], frame->proc[1], 
        ic_output, &input_vnr_pred);
    frame->metadata.vnr_pred_flag = input_vnr_pred;

    memcpy(&frame->proc[0][0], &ic_output[0], VOICE_PIPELINE_FRAME_ADVANCE*sizeof(int32_t));
}

void stage_ns(voice_pipeline_states_t *states, voice_pipeline_frame_t *frame) {
    /** NS*/
    int32_t ns_output[VOICE_PIPELINE_FRAME_ADVANCE];

    ns_process_frame(&states->ns_state, ns_output, &frame->proc[0][0]);
    memcpy(&frame->proc[0][0], &ns_output[0], VOICE_PIPELINE_FRAME_ADVANCE*sizeof(int32_t));
}

void stage_agc(voice_pipeline_states_t *states, voice_pipeline_frame_t *frame) {
    /** AGC*/
    agc_meta_data_t agc_md;
    agc_md.aec_ref_power = frame->metadata.max_ref_energy;
    agc_md.vnr_flag = frame->metadata.vnr_pred_flag;
    agc_md.ref_active_flag = frame->metadata.ref_active_flag;
    agc_md.aec_corr_factor = frame->metadata.aec_corr_factor[0];
    int32_t agc_output[VOICE_PIPELINE_FRAME_ADVANCE];

    agc_process_frame(&states->agc_state, &agc_output[0], &frame->proc[0][0], &agc_md);
    memcpy(&frame->proc[0][0], &agc_output[0], VOICE_PIPELINE_FRAME_ADVANCE*sizeof(int32_t));
}

DEFINE_INTERRUPT_CALLBACK(VOICE_PIPELINE_ISR_GRP, voice_pipeline_isr, app_data){
    voice_pipeline_handle_t* handle = app_data;

    transacting_chanend_t t_voice = chan_init_transaction_slave(handle->c_voice);
    size_t vp_need_reset = t_chan_in_word(&t_voice);
    if (vp_need_reset) {
        // Reset the frame index
        handle->frame_idx = 0;
    }
    // Hacky way to do this, treating ref mic proc as one multi channel buf
    for (int i = 0; i < VOICE_PIPELINE_EXCHANGE_NUM_CHANNEL; ++i) {
        handle->frame_pool[handle->in_pool_idx].ref[i][handle->frame_idx] = t_chan_in_word(&t_voice);
    }
    for (int i = 0; i < VOICE_PIPELINE_EXCHANGE_NUM_CHANNEL; ++i) {
        t_chan_out_word(&t_voice, handle->frame_pool[handle->out_pool_idx].ref[i][handle->frame_idx]);
    }
    chan_complete_transaction(t_voice);

    if (++handle->frame_idx >= VOICE_PIPELINE_FRAME_ADVANCE) {
        // Notify pipeline task of the new buffer index
        chanend_out_word(handle->c_buf_idx, handle->in_pool_idx);
        // Swap the frame pointers for the next frame
        handle->frame_idx = 0;
        handle->in_pool_idx = (handle->in_pool_idx + 1) % VOICE_PIPELINE_FRAME_POOL_SIZE;
        handle->out_pool_idx = (handle->out_pool_idx + 1) % VOICE_PIPELINE_FRAME_POOL_SIZE;
    }
}

DEFINE_INTERRUPT_PERMITTED(VOICE_PIPELINE_ISR_GRP, void, voice_pipeline_task, chanend_t c_voice) {
    voice_pipeline_states_t states;
    voice_pipeline_handle_t handle = {
        .in_pool_idx = VOICE_PIPELINE_FRAME_POOL_SIZE-1,
        .out_pool_idx = 0,
        .frame_idx = 0,
    };
    int st, et;
    
    // We use a single chanend to send the buffer IDX from the ISR of this task back to voice pipeline and sync
    chanend_t c_buf_idx = chanend_alloc();
    chanend_set_dest(c_buf_idx, c_buf_idx); // Loopback chanend to itself
    
    handle.c_voice = c_voice;
    handle.c_buf_idx = c_buf_idx;

    // Enable interrupt on channel receive token (sent from ISR)
    triggerable_setup_interrupt_callback(c_voice, &handle, INTERRUPT_CALLBACK(voice_pipeline_isr));
    triggerable_enable_trigger(c_voice);
    interrupt_unmask_all();

    stage_stage1_init(&states);
    stage_vnr_ic_init(&states);
    stage_ns_init(&states);
    stage_agc_init(&states);

    SELECT_RES(
        CASE_THEN(c_buf_idx, pipeline_handle)
    ) {
        pipeline_handle: {
            size_t buf_idx = chanend_in_word(c_buf_idx);
            // Voice Pipeline Processing Start
            asm volatile("gettime %0" : "=r"(st));
            // stage_bypass(&states, &handle.frame_pool[buf_idx]);
            stage_stage1(&states, &handle.frame_pool[buf_idx]);
            stage_vnr_ic(&states, &handle.frame_pool[buf_idx]);
            stage_ns(&states, &handle.frame_pool[buf_idx]);
            stage_agc(&states, &handle.frame_pool[buf_idx]);
            // Voice Pipeline Processing End
            asm volatile("gettime %0" : "=r"(et));
            // printf("Voice pipeline processing time: %d\n", et - st);
            continue;
        }
    }
}

void voice_pipeline(chanend_t c_voice) {
    // Wrapper for the real voice pipeline
    INTERRUPT_PERMITTED(voice_pipeline_task)(c_voice);
}