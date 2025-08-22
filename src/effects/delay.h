/* delay.h
 * Author: Milan Wendt
 * Date:   2025-08-19
 *
 * Copyright (c) 2025 Milan Wendt
 *
 * This file is part of the RP2040-DSP project.
 *
 * This project (in the current state) is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3 as published by the Free Software Foundation.
 *
 * RP2040 DSP is distributed in the hope that it will
 * be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this project. 
 * If not, see <https://www.gnu.org/licenses/>.
 */
#ifndef DELAY_H
#define DELAY_H

#include <stdint.h>
#include <string.h>
#include "spi_ram.h"

// === Constants ===
#define MAX_DELAY_SAMPLES  98304      // ~2 sec at 48 kHz
#define BLOCK_SIZE         AUDIO_BUFFER_FRAMES // Make RAM delay samples and block size match
#define SPI_BLOCK_COUNT    (MAX_DELAY_SAMPLES / BLOCK_SIZE)

// === Parameters ===
static uint32_t delay_samples_l = 48000;
static uint32_t delay_samples_r = 48000;
static uint32_t delay_feedback_q16 = Q16_ONE / 4;
static uint32_t delay_mix_q16 = Q16_ONE / 2;
static uint32_t delay_dry_q16 = Q16_ONE / 2; // computed as 1 - mix
static uint32_t volume_gain_q16 = Q16_ONE;

// === LPF parameters ===
static uint32_t lpf_alpha_q16 = Q16_ONE / 4;
static int32_t lpf_state_l = 0;
static int32_t lpf_state_r = 0;

// === Left channel state ===
static uint32_t spi_write_index_l = 0, spi_read_index_l = 0;
static int32_t write_block_l[BLOCK_SIZE], read_block_l[BLOCK_SIZE];
static uint32_t write_block_pos_l = 0, write_block_index_l = 0, read_block_start_index_l = 0;

// === Right channel state ===
static uint32_t spi_write_index_r = 0, spi_read_index_r = 0;
static int32_t write_block_r[BLOCK_SIZE], read_block_r[BLOCK_SIZE];
static uint32_t write_block_pos_r = 0, write_block_index_r = 0, read_block_start_index_r = 0;

// === SPI helpers ===
static inline void spi_write_block(uint32_t block_index, int32_t* block, uint32_t base_offset) {
    uint32_t addr = base_offset + block_index * BLOCK_SIZE * 4;
    uint8_t tmp_buf[BLOCK_SIZE * 4];
    for (uint32_t i = 0; i < BLOCK_SIZE; i++) {
        tmp_buf[i * 4 + 0] = (block[i] >> 24) & 0xFF;
        tmp_buf[i * 4 + 1] = (block[i] >> 16) & 0xFF;
        tmp_buf[i * 4 + 2] = (block[i] >> 8) & 0xFF;
        tmp_buf[i * 4 + 3] = block[i] & 0xFF;
    }
    spi_ram_write_burst(addr, tmp_buf, BLOCK_SIZE * 4);
}

static inline void spi_read_block(uint32_t block_index, int32_t* block, uint32_t base_offset) {
    uint32_t addr = base_offset + block_index * BLOCK_SIZE * 4;
    uint8_t tmp_buf[BLOCK_SIZE * 4];
    spi_ram_read_burst(addr, tmp_buf, BLOCK_SIZE * 4);
    for (uint32_t i = 0; i < BLOCK_SIZE; i++) {
        block[i] = (tmp_buf[i * 4 + 0] << 24) |
                   (tmp_buf[i * 4 + 1] << 16) |
                   (tmp_buf[i * 4 + 2] << 8)  |
                   (tmp_buf[i * 4 + 3]);
    }
}

// === Initialization ===
static inline void init_delay(void) {
    memset(write_block_l, 0, sizeof(write_block_l));
    memset(write_block_r, 0, sizeof(write_block_r));

    int32_t tmp_block[BLOCK_SIZE] = {0};

    for (uint32_t i = 0; i < SPI_BLOCK_COUNT / 2; i++) {
        spi_write_block(i, tmp_block, 0);
        spi_write_block(i, tmp_block, MAX_DELAY_SAMPLES * 4 / 2);
    }

    // Left
    spi_read_index_l = 0;
    spi_write_index_l = delay_samples_l % MAX_DELAY_SAMPLES;
    write_block_index_l = (spi_write_index_l / BLOCK_SIZE) % (SPI_BLOCK_COUNT / 2);
    write_block_pos_l = spi_write_index_l % BLOCK_SIZE;

    read_block_start_index_l = spi_read_index_l / BLOCK_SIZE;
    spi_read_block(read_block_start_index_l % (SPI_BLOCK_COUNT / 2), read_block_l, 0);

    // Right
    spi_read_index_r = 0;
    spi_write_index_r = delay_samples_r % MAX_DELAY_SAMPLES;
    write_block_index_r = (spi_write_index_r / BLOCK_SIZE) % (SPI_BLOCK_COUNT / 2);
    write_block_pos_r = spi_write_index_r % BLOCK_SIZE;

    read_block_start_index_r = spi_read_index_r / BLOCK_SIZE;
    spi_read_block(read_block_start_index_r % (SPI_BLOCK_COUNT / 2), read_block_r, MAX_DELAY_SAMPLES * 4 / 2);
}

static inline void clear_delay_memory(void) {
    int32_t tmp_block[BLOCK_SIZE] = {0};

    // Clear left half
    for (uint32_t i = 0; i < SPI_BLOCK_COUNT / 2; i++) {
        spi_write_block(i, tmp_block, 0);
    }

    // Clear right half
    for (uint32_t i = 0; i < SPI_BLOCK_COUNT / 2; i++) {
        spi_write_block(i, tmp_block, MAX_DELAY_SAMPLES * 4 / 2);
    }

    // Also reset states
    memset(write_block_l, 0, sizeof(write_block_l));
    memset(read_block_l, 0, sizeof(read_block_l));
    memset(write_block_r, 0, sizeof(write_block_r));
    memset(read_block_r, 0, sizeof(read_block_r));

    lpf_state_l = 0;
    lpf_state_r = 0;

    // Reset read/write indexes
    spi_read_index_l = 0;
    spi_write_index_l = delay_samples_l % MAX_DELAY_SAMPLES;
    write_block_index_l = (spi_write_index_l / BLOCK_SIZE) % (SPI_BLOCK_COUNT / 2);
    write_block_pos_l = spi_write_index_l % BLOCK_SIZE;
    read_block_start_index_l = spi_read_index_l / BLOCK_SIZE;
    spi_read_block(read_block_start_index_l % (SPI_BLOCK_COUNT / 2), read_block_l, 0);

    spi_read_index_r = 0;
    spi_write_index_r = delay_samples_r % MAX_DELAY_SAMPLES;
    write_block_index_r = (spi_write_index_r / BLOCK_SIZE) % (SPI_BLOCK_COUNT / 2);
    write_block_pos_r = spi_write_index_r % BLOCK_SIZE;
    read_block_start_index_r = spi_read_index_r / BLOCK_SIZE;
    spi_read_block(read_block_start_index_r % (SPI_BLOCK_COUNT / 2), read_block_r, MAX_DELAY_SAMPLES * 4 / 2);
}

// === Main process (sample-based) ===
static inline void process_audio_delay_sample(int32_t* inout_l, int32_t* inout_r, DelayMode mode) {
    // === Compute block info ===
    uint32_t block_idx_l = spi_read_index_l / BLOCK_SIZE;
    uint32_t offset_l    = spi_read_index_l % BLOCK_SIZE;
    uint32_t wrapped_l   = block_idx_l % (SPI_BLOCK_COUNT / 2);

    uint32_t block_idx_r = spi_read_index_r / BLOCK_SIZE;
    uint32_t offset_r    = spi_read_index_r % BLOCK_SIZE;
    uint32_t wrapped_r   = block_idx_r % (SPI_BLOCK_COUNT / 2);

    // === Read blocks ===
    if (offset_l == 0) spi_read_block(wrapped_l, read_block_l, 0);
    if (offset_r == 0) spi_read_block(wrapped_r, read_block_r, MAX_DELAY_SAMPLES * 4 / 2);

    // === Get delayed samples ===
    int32_t delayed_l = read_block_l[offset_l];
    int32_t delayed_r = read_block_r[offset_r];

    // === Feedback inputs based on mode ===
    int32_t fb_l, fb_r;
    int32_t pre_lpf_l, pre_lpf_r;

    switch (mode) {
        case DELAY_MODE_PARALLEL:
            fb_l = multiply_q16(delayed_l, delay_feedback_q16);
            fb_r = multiply_q16(delayed_r, delay_feedback_q16);
            pre_lpf_l = *inout_l + fb_l;
            pre_lpf_r = *inout_r + fb_r;
            break;

        case DELAY_MODE_CROSS:
            fb_l = multiply_q16(delayed_r, delay_feedback_q16);  // Right feeds into Left
            fb_r = multiply_q16(delayed_l, delay_feedback_q16);  // Left feeds into Right

            pre_lpf_l = *inout_l + fb_l;
            pre_lpf_r = *inout_r + fb_r;
            break;
        
        case DELAY_MODE_MIXED:
            fb_l = multiply_q16((delayed_l + delayed_r) >> 1, delay_feedback_q16);  // Mixed feedback
            fb_r = fb_l;  // Same value for both
            pre_lpf_l = *inout_l + fb_l;
            pre_lpf_r = *inout_r + fb_r;
            break;

        case DELAY_MODE_PINGPONG:
            int32_t mono_input = (*inout_l >> 1) + (*inout_r >> 1);

            int32_t fb_l = multiply_q16(delayed_r, delay_feedback_q16);
            int32_t pre_lpf_l = mono_input + fb_l;
            lpf_state_l += multiply_q16((pre_lpf_l - lpf_state_l), lpf_alpha_q16);
            int32_t to_store_l = lpf_state_l;
            write_block_l[write_block_pos_l++] = to_store_l;

            int32_t fb_r = multiply_q16(delayed_l, delay_feedback_q16);
            int32_t pre_lpf_r = fb_r;
            lpf_state_r += multiply_q16((pre_lpf_r - lpf_state_r), lpf_alpha_q16);
            int32_t to_store_r = lpf_state_r;
            write_block_r[write_block_pos_r++] = to_store_r;

            // === Handle block writes ===
            if (write_block_pos_l >= BLOCK_SIZE) {
                spi_write_block(write_block_index_l, write_block_l, 0);
                write_block_index_l = (write_block_index_l + 1) % (SPI_BLOCK_COUNT / 2);
                write_block_pos_l = 0;
            }

            if (write_block_pos_r >= BLOCK_SIZE) {
                spi_write_block(write_block_index_r, write_block_r, MAX_DELAY_SAMPLES * 4 / 2);
                write_block_index_r = (write_block_index_r + 1) % (SPI_BLOCK_COUNT / 2);
                write_block_pos_r = 0;
            }

            // === Output mix ===
            *inout_l = multiply_q16(*inout_l, delay_dry_q16) + multiply_q16(delayed_l, delay_mix_q16);
            *inout_r = multiply_q16(*inout_r, delay_dry_q16) + multiply_q16(delayed_r, delay_mix_q16);
            *inout_l = multiply_q16(*inout_l, volume_gain_q16);
            *inout_r = multiply_q16(*inout_r, volume_gain_q16);

            // === Update delay indices ===
            spi_write_index_l = (spi_write_index_l + 1) % MAX_DELAY_SAMPLES;
            spi_read_index_l  = (spi_write_index_l + MAX_DELAY_SAMPLES - delay_samples_l) % MAX_DELAY_SAMPLES;

            spi_write_index_r = (spi_write_index_r + 1) % MAX_DELAY_SAMPLES;
            spi_read_index_r  = (spi_write_index_r + MAX_DELAY_SAMPLES - delay_samples_r) % MAX_DELAY_SAMPLES;
            return; // Early return for ping-pong mode
    }
    
    // === LPF and write to buffer ===
    lpf_state_l += multiply_q16((pre_lpf_l - lpf_state_l), lpf_alpha_q16);
    lpf_state_r += multiply_q16((pre_lpf_r - lpf_state_r), lpf_alpha_q16);

    write_block_l[write_block_pos_l++] = lpf_state_l;
    write_block_r[write_block_pos_r++] = lpf_state_r;

    if (write_block_pos_l >= BLOCK_SIZE) {
        spi_write_block(write_block_index_l, write_block_l, 0);
        write_block_index_l = (write_block_index_l + 1) % (SPI_BLOCK_COUNT / 2);
        write_block_pos_l = 0;
    }

    if (write_block_pos_r >= BLOCK_SIZE) {
        spi_write_block(write_block_index_r, write_block_r, MAX_DELAY_SAMPLES * 4 / 2);
        write_block_index_r = (write_block_index_r + 1) % (SPI_BLOCK_COUNT / 2);
        write_block_pos_r = 0;
    }

    // === Mix dry and wet ===
    *inout_l = multiply_q16(*inout_l, delay_dry_q16) + multiply_q16(delayed_l, delay_mix_q16);
    *inout_r = multiply_q16(*inout_r, delay_dry_q16) + multiply_q16(delayed_r, delay_mix_q16);

    *inout_l = multiply_q16(*inout_l, volume_gain_q16);
    *inout_r = multiply_q16(*inout_r, volume_gain_q16);

    // === Update indices ===
    spi_write_index_l = (spi_write_index_l + 1) % MAX_DELAY_SAMPLES;
    spi_read_index_l  = (spi_write_index_l + MAX_DELAY_SAMPLES - delay_samples_l) % MAX_DELAY_SAMPLES;

    spi_write_index_r = (spi_write_index_r + 1) % MAX_DELAY_SAMPLES;
    spi_read_index_r  = (spi_write_index_r + MAX_DELAY_SAMPLES - delay_samples_r) % MAX_DELAY_SAMPLES;
}

#define PERCH_DELAY_SAMPLES   (MAX_DELAY_SAMPLES / 2)
#define MIN_DELAY_SAMPLES     (SAMPLE_RATE / 1000) // 1 ms worth of samples

// === Load parameters from memory ===
static inline void load_delay_parms_from_memory(void) {
    delay_samples_l = MIN_DELAY_SAMPLES +
        ((uint32_t)storedPotValue[DELAY_EFFECT_INDEX][0] * (PERCH_DELAY_SAMPLES - MIN_DELAY_SAMPLES)) / POT_MAX;

    delay_samples_r = MIN_DELAY_SAMPLES +
        ((uint32_t)storedPotValue[DELAY_EFFECT_INDEX][1] * (PERCH_DELAY_SAMPLES - MIN_DELAY_SAMPLES)) / POT_MAX;
    delay_feedback_q16 = ((uint32_t)storedPotValue[DELAY_EFFECT_INDEX][2] * Q16_ONE) / POT_MAX;
    delay_mix_q16      = ((uint32_t)storedPotValue[DELAY_EFFECT_INDEX][3] * Q16_ONE) / POT_MAX;
    delay_dry_q16      = Q16_ONE - delay_mix_q16;

    float min_alpha = 0.05f;
    float pot_fraction = (float)storedPotValue[DELAY_EFFECT_INDEX][4] / (float)POT_MAX;
    float alpha_f = min_alpha + pot_fraction * (1.0f - min_alpha);
    lpf_alpha_q16 = float_to_q16(alpha_f);

    float min_gain = 0.1f;
    float max_gain = 2.5f;
    float gain_fraction = (float)storedPotValue[DELAY_EFFECT_INDEX][5] / (float)POT_MAX;
    float gain_f = min_gain + gain_fraction * (max_gain - min_gain);
    volume_gain_q16 = float_to_q16(gain_f);

    spi_read_index_l = (spi_write_index_l + MAX_DELAY_SAMPLES - delay_samples_l) % MAX_DELAY_SAMPLES;
    spi_read_index_r = (spi_write_index_r + MAX_DELAY_SAMPLES - delay_samples_r) % MAX_DELAY_SAMPLES;
}

// === Update parameters from pots ===
static inline void update_delay_params_from_pots(int changed_pot) {
    if (changed_pot < 0) return;
    storedPotValue[DELAY_EFFECT_INDEX][changed_pot] = pot_value[changed_pot];
    load_delay_parms_from_memory();
}

void delay_process_block(int32_t* in_l, int32_t* in_r, size_t frames, DelayMode mode) {
    for (size_t i = 0; i < frames; i++) {
        process_audio_delay_sample(&in_l[i], &in_r[i], mode);
    }
}

#endif // DELAY_H
