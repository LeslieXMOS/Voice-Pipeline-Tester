#include <stdio.h>
#include <string.h>
#include <xs1.h>
#include <platform.h>
#include <xcore/parallel.h>
#include <xcore/select.h>
#include <xcore/channel.h>
#include <xcore/channel_transaction.h>
#include <xcore/port.h>
#include <xcore/port_protocol.h>
#include <xcore/clock.h>
#include <xcore/triggerable.h>
#include <xcore/interrupt_wrappers.h>
#include <xcore/interrupt.h>
#include <xcore/hwtimer.h>
#include <xclib.h>
#include <xscope.h>
#include "voice_pipeline.h"
#include "flash_server.h"
#include "xscope_io_device.h"

#define XSCOPE_FRAME_ADVANCE    (VOICE_PIPELINE_FRAME_ADVANCE)

#define N_NETWORKS      1
#define NFLASH_SPECS    1

const char out_file_name[] = "host/temp/out.bin";
const char audio_file_name[] = "host/temp/audio.bin";

void main_tile0(chanend_t c_tile1, unsigned tile1_id)
{
    (void) tile1_id;
    
    voice_pipeline(c_tile1);
}

void main_tile1(chanend_t c_tile0, unsigned tile0_id)
{
    (void) tile0_id;
    
    chanend_t c_xscope = chanend_alloc();
    size_t num_bytes = 0;
    xscope_io_init(c_xscope);
    
    xscope_file_t out_file = xscope_open_file(out_file_name, "wb");
    xscope_file_t in_file = xscope_open_file(audio_file_name, "rb");

    int32_t in_buffer[XSCOPE_FRAME_ADVANCE][VOICE_PIPELINE_EXCHANGE_NUM_CHANNEL];
    int32_t out_buffer[XSCOPE_FRAME_ADVANCE][VOICE_PIPELINE_EXCHANGE_NUM_CHANNEL];

    hwtimer_t timer = hwtimer_alloc();

    do {
        unsigned st, et, delta;
        asm volatile("gettime %0" : "=r"(st));
        memset(in_buffer, 0, sizeof(in_buffer));
        num_bytes = xscope_fread(&in_file, (uint8_t*)in_buffer, sizeof(in_buffer));

        for (int cnt = 0; cnt < XSCOPE_FRAME_ADVANCE; ++cnt) {
            transacting_chanend_t t_voice = chan_init_transaction_master(c_tile0);
            t_chan_out_word(&t_voice, 0);
            for (int i = 0; i < VOICE_PIPELINE_EXCHANGE_NUM_CHANNEL; ++i) {
                t_chan_out_word(&t_voice, in_buffer[cnt][i]);
            }
            for (int i = 0; i < VOICE_PIPELINE_EXCHANGE_NUM_CHANNEL; ++i) {
                out_buffer[cnt][i] = t_chan_in_word(&t_voice);
            }
            chan_complete_transaction(t_voice);
        }

        xscope_fwrite(&out_file, (uint8_t*)out_buffer, sizeof(out_buffer));
        asm volatile("gettime %0" : "=r"(et));
        delta = et - st;
        if (delta < 1500000)
            hwtimer_delay(timer, 1500000 - delta);
    } while (num_bytes);

    xscope_fclose(&in_file);
    xscope_fclose(&out_file);

    xscope_close_all_files();
    chanend_free(c_xscope);

    printf("Tile1 Ended\n");
}