// Copyright 2026 XMOS LIMITED.
// This Software is subject to the terms of the XMOS Public Licence: Version 1.
#include "voice.h"

#if ALT_ARCH_MODE

static inline void get_delayed_frame(
        int32_t (*input_y_data)[AEC_FRAME_ADVANCE],
        int32_t (*input_x_data)[AEC_FRAME_ADVANCE],
        delay_buf_state_t *delay_state)
{
    int num_channels = (delay_state->delay_samples) > 0 ? AEC_MAX_Y_CHANNELS : AEC_MAX_X_CHANNELS;
    if (delay_state->delay_samples >= 0) {/** Requested Mic delay +ve => delay mic*/
        for(int ch=0; ch<num_channels; ch++) {
            for(int i=0; i<AEC_FRAME_ADVANCE; i++) {
                get_delayed_sample(delay_state, &input_y_data[ch][i], ch);
            }
        }
    }
    else if (delay_state->delay_samples < 0) {/* Requested Mic delay negative => advance mic which can't be done, so delay reference*/
        for(int ch=0; ch<num_channels; ch++) {
            for(int i=0; i<AEC_FRAME_ADVANCE; i++) {
                get_delayed_sample(delay_state, &input_x_data[ch][i], ch);
            }
        }
    }
    return;
}

// Based of activity on the reference channels, this function controls enabling and disabling of AEC and IC stages.
static void alt_arch_controller(stage1_t *state, int32_t *ref_active_flag) {
    if(*ref_active_flag){ //Ref present
        // If there's reference, enable AEC and disable IC right away
        state->hold_aec_count = 0;
        state->aec_state.main_state.shared_state->config_params.aec_core_conf.bypass = 0;
    }
    else { //Ref absent
        if(!state->aec_state.main_state.shared_state->config_params.aec_core_conf.bypass) { // If reference is not there and AEC is still enabled
            if(state->hold_aec_count > state->hold_aec_limit) { // If reference has been absent for 3 continuous seconds, disable AEC
                state->aec_state.main_state.shared_state->config_params.aec_core_conf.bypass = 1;
            }
            else { // If ref hasn't been absent for 3 continuous seconds, keep AEC enabled and propagate ref as being present to the next stage.
                state->hold_aec_count++;
                // propagate the ref_active_flag still as 1 to the next stage
                *ref_active_flag = 1;
            }
        }
    }
}

// In alt arch mode AEC outputs 1 channel and IC works on 2 input channel. This function makes sure that proper number of channels of output data is sent
// out of this stage. It assumes alt arch design, i.e when AEC is enabled, IC is disabled and vice versa.
static void alt_arch_rewrite_output(int32_t (*output)[AEC_FRAME_ADVANCE], const int32_t (*mic_input)[AEC_FRAME_ADVANCE], int32_t y_channels, int32_t aec_bypass) {
    // This code implies knowledge of the other pipeline stages which this stage is ideally not supposed to have, but alt-arch design
    // assumes that stage 1 has this knowledge and gets to make decisions about enabling/disabling downstream stages.

    /** If we've processed fewer channels than the max present in the pipeline*/
    if(y_channels < AEC_MAX_Y_CHANNELS) {
        // If AEC is not bypassed, copy AEC output to the other channels that haven't been processed by AEC. This is the alt arch situation
        // where 1 channel AEC is enabled and IC is bypassed. We're assuming here that since AEC is enabled, IC would be disabled and so the
        // 2 channels of duplicate output would not be processed through IC.
        if(!aec_bypass)
        {
            for(int ch=y_channels; ch<AEC_MAX_Y_CHANNELS; ch++)
            {
                memcpy(&output[ch][0], &output[y_channels - 1][0], AEC_FRAME_ADVANCE*sizeof(int32_t));
            }
        }
        else {
            // If AEC is bypassed, copy the mic input to all the output channels. This is the alt arch situation where aec is bypassed and
            // IC is enabled. Since AEC has only bypassed one channel and IC would need both channels with their original phase relationship
            // preserved, we overwrite the AEC output with mic input. Providing 1 channel of AEC bypassed output and routing the other mic channel
            // unmodified to IC doesn't work for IC.
            for(int ch=0; ch<AEC_MAX_Y_CHANNELS; ch++) {
                memcpy(&output[ch][0], &mic_input[ch][0], AEC_FRAME_ADVANCE*sizeof(int32_t));// AEC cannot process the frame in-place because of this
            }
        }
    }
}

/** Process a frame of data through AEC and fixed delay*/
void stage1_fixed_delay_alt_arch_process_frame(
    stage1_t *state, int32_t (*output_frame)[AEC_FRAME_ADVANCE],
    float_s32_t *max_ref_energy, float_s32_t *aec_corr_factor, int32_t *ref_active_flag,
    int32_t (*input_y)[AEC_FRAME_ADVANCE], int32_t (*input_x)[AEC_FRAME_ADVANCE])
{
    delay_buf_state_t *delay_state_ptr = &state->delay_state;
    get_delayed_frame(
            input_y,
            input_x,
            delay_state_ptr
            );

    /** Alt-arch controller logic*/
    /** Detect if there's activity on the reference channels*/
    *ref_active_flag = aec_detect_input_activity(input_x, state->ref_active_threshold, state->aec_state.main_state.shared_state->num_x_channels);
    alt_arch_controller(state, ref_active_flag);

    /** AEC*/
    aec_process_frame(&state->aec_state, output_frame, NULL, input_y, input_x);

    /** Update metadata*/
    *max_ref_energy = aec_calc_max_input_energy(input_x, state->aec_state.main_state.shared_state->num_x_channels);
    for(int ch=0; ch<state->aec_state.main_state.shared_state->num_y_channels; ch++) {
        aec_corr_factor[ch] = aec_calc_corr_factor(&state->aec_state.main_state, ch);
    }

    alt_arch_rewrite_output(output_frame, input_y, state->aec_state.main_state.shared_state->num_y_channels, state->aec_state.main_state.shared_state->config_params.aec_core_conf.bypass);
}

#endif