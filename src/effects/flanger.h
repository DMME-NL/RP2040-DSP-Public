/* flanger.h
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
#ifndef FLANGER_H
#define FLANGER_H

#include <stdint.h>
#include <string.h>
#include <math.h>

// === Constants ===
#define FLANGER_MAX_SAMPLES 256
#define FLANGER_MIN_DELAY_SAMPLES 8

// === Delay line ===
static int32_t flanger_buffer_l[FLANGER_MAX_SAMPLES] = {0};
static int32_t flanger_buffer_r[FLANGER_MAX_SAMPLES] = {0};
static uint32_t flanger_write_pos = 0;

// === Parameters ===
static uint32_t flanger_depth_q16    = Q16_ONE / 2;
static uint32_t flanger_speed_q16    = Q16_ONE / 4;
static uint32_t flanger_feedback_q16 = 0;
static uint32_t flanger_mix_q16      = Q16_ONE / 2;
static uint32_t flanger_volume_q24   = Q24_ONE;

// === LFO state ===
static uint32_t flanger_lfo_phase_l = 0;
static uint32_t flanger_lfo_phase_r = 0x80000000;  // 180 degrees phase shift
static uint32_t flanger_lfo_inc = 0;

extern bool lfo_led_state;

// === LPF states ===
static int32_t flanger_lpf_state_l = 0;
static int32_t flanger_lpf_state_r = 0;
static uint32_t flanger_lpf_coef_q16 = 0x4000; // default

// --- Thiran states (per channel) ---
static int32_t fl_thiran_y_prev[2] = {0,0};  // [0]=L, [1]=R

// 1st-order Thiran all-pass fractional delay
// mu = frac in [0,1) (Q16). Base index points to x[n-1].
static inline int32_t fl_read_thiran_q16(
    const int32_t *buf,
    uint32_t base_idx,   // x[n-1]
    uint32_t frac_q16,   // mu in Q16
    int chan_index       // 0=L, 1=R
){
    uint32_t i0 = base_idx;                                  // x[n-1]
    uint32_t i1 = (base_idx + 1) % FLANGER_MAX_SAMPLES;      // x[n]
    int32_t x0 = buf[i0];
    int32_t x1 = buf[i1];

    // a = (1 - mu) / (1 + mu)  (Q16)
    uint32_t one = Q16_ONE;
    uint32_t num = (frac_q16 >= one) ? 0 : (one - frac_q16);
    uint32_t den = one + frac_q16;
    uint32_t a_q16 = (uint32_t)(((uint64_t)num << 16) / (uint64_t)den);

    // y[n] = -a*y[n-1] + x[n-1] + a*x[n]
    int64_t acc = x0;
    acc += ((int64_t)a_q16 * x1) >> 16;
    acc -= ((int64_t)a_q16 * fl_thiran_y_prev[chan_index]) >> 16;

    int32_t y = (int32_t)acc;
    fl_thiran_y_prev[chan_index] = y;
    return y;
}

static inline int32_t flanger_process_lpf_q16(int32_t x, int32_t *state, uint32_t coef_q16) {
    int32_t y = ((int64_t)(Q16_ONE - coef_q16) * x + (int64_t)coef_q16 * (*state)) >> 16;
    *state = y;
    return y;
}

// === Init ===
static inline void init_flanger(void) {
    memset(flanger_buffer_l, 0, sizeof(flanger_buffer_l));
    memset(flanger_buffer_r, 0, sizeof(flanger_buffer_r));
    flanger_write_pos = 0;
    flanger_lfo_phase_l = 0;
    flanger_lfo_phase_r = 0x80000000;
    flanger_lpf_state_l = 0;
    flanger_lpf_state_r = 0;
}

// === Load Parameters ===
static inline void load_flanger_parms_from_memory(void) {
    int32_t pot;

    // Speed: 0.05 to 5 Hz
    pot = storedPotValue[FLNG_EFFECT_INDEX][0];
    float hz = 0.05f + ((float)pot / POT_MAX) * (5.0f - 0.05f);
    flanger_lfo_inc = (uint32_t)((hz / SAMPLE_RATE) * 4294967296.0f);

    // Depth: 0 to 1
    pot = storedPotValue[FLNG_EFFECT_INDEX][1];
    flanger_depth_q16 = map_pot_to_q16(pot, 0, Q16_ONE);

    // Feedback: 0 to 0.9
    pot = storedPotValue[FLNG_EFFECT_INDEX][2];
    flanger_feedback_q16 = map_pot_to_q16(pot, 0, (uint32_t)(0.9f * Q16_ONE));

    // LPF cutoff: 100 Hz to 8 kHz (pot #4)
    pot = storedPotValue[FLNG_EFFECT_INDEX][4];
    float min_hz = 100.0f;
    float max_hz = 8000.0f;
    float norm = (float)pot / POT_MAX;
    float freq_hz = min_hz * powf(max_hz / min_hz, norm);

    float alpha = expf(-2.0f * 3.1415926f * freq_hz / SAMPLE_RATE);
    if (alpha < 0.0f) alpha = 0.0f;
    if (alpha > 1.0f) alpha = 1.0f;
   flanger_lpf_coef_q16 = float_to_q16(alpha);

    // Mix: 0 to 1
    pot = storedPotValue[FLNG_EFFECT_INDEX][3];
    flanger_mix_q16 = map_pot_to_q16(pot, 0, Q16_ONE);

    // Volume: 0.1 to 3.0
    pot = storedPotValue[FLNG_EFFECT_INDEX][5];
    flanger_volume_q24 = map_pot_to_q24(pot, float_to_q24(0.1f), float_to_q24(3.0f));
}

static inline void update_flanger_params_from_pots(int changed_pot) {
    if (changed_pot < 0) return;
    storedPotValue[FLNG_EFFECT_INDEX][changed_pot] = pot_value[changed_pot];
    load_flanger_parms_from_memory();
}

// === Process Sample ===
static inline void process_audio_flanger_sample(int32_t* inout_l, int32_t* inout_r, FXmode mode) {
    // Advance left once
    flanger_lfo_phase_l += flanger_lfo_inc;

    // Derive right from left (wrap on uint32_t is fine)
    flanger_lfo_phase_r = flanger_lfo_phase_l + (mode == FX_MONO ? 0u : 0x80000000u);

    const uint32_t max_depth_samples = FLANGER_MAX_SAMPLES - FLANGER_MIN_DELAY_SAMPLES - 4;

    // Left
    // LFO (Q16), keep full precision through the mapping
    uint32_t lfo_q16_l = lfo_q16_shape(flanger_lfo_phase_l, LFO_TRIANGLE);

    // Q16 * Q16 = Q32 (no early >>16!)
    uint64_t scaled_full_l_q32 = (uint64_t)lfo_q16_l * (uint64_t)flanger_depth_q16;

    // offset in Q16.16: (max_depth << 16) * scaled_q32 >> 32
    uint32_t offset_l_q16 = (uint32_t)((((uint64_t)max_depth_samples << 16) * scaled_full_l_q32) >> 32);

    // target delay in Q16.16
    uint32_t delay_l_q16 = (FLANGER_MIN_DELAY_SAMPLES << 16) + offset_l_q16;

    // extract integer/fractional parts
    uint32_t int_delay_l = delay_l_q16 >> 16;
    uint32_t frac_q16_l  = delay_l_q16 & 0xFFFF;

    // base index one behind the write head
    uint32_t base_l = (flanger_write_pos + FLANGER_MAX_SAMPLES - int_delay_l - 1) % FLANGER_MAX_SAMPLES;

    // Thiran all-pass read (flat magnitude)
    int32_t delayed_l = fl_read_thiran_q16(flanger_buffer_l, base_l, frac_q16_l, 0);


    // Right
    uint32_t lfo_q16_r = lfo_q16_shape(flanger_lfo_phase_r, LFO_TRIANGLE);
    uint64_t scaled_full_r_q32 = (uint64_t)lfo_q16_r * (uint64_t)flanger_depth_q16;

    uint32_t offset_r_q16 = (uint32_t)((((uint64_t)max_depth_samples << 16) * scaled_full_r_q32) >> 32);
    uint32_t delay_r_q16 = (FLANGER_MIN_DELAY_SAMPLES << 16) + offset_r_q16;

    uint32_t int_delay_r = delay_r_q16 >> 16;
    uint32_t frac_q16_r  = delay_r_q16 & 0xFFFF;

    uint32_t base_r = (flanger_write_pos + FLANGER_MAX_SAMPLES - int_delay_r - 1) % FLANGER_MAX_SAMPLES;
    int32_t delayed_r = fl_read_thiran_q16(flanger_buffer_r, base_r, frac_q16_r, 1);


    // Feedback
    int32_t fb_l = (int32_t)(((int64_t)delayed_l * flanger_feedback_q16) >> 16);
    int32_t fb_r = (int32_t)(((int64_t)delayed_r * flanger_feedback_q16) >> 16);

    int32_t new_l = *inout_l + fb_l;
    int32_t new_r = *inout_r + fb_r;

    flanger_buffer_l[flanger_write_pos] = new_l;
    flanger_buffer_r[flanger_write_pos] = new_r;

    // LPF smoothing
    delayed_l = flanger_process_lpf_q16(delayed_l, &flanger_lpf_state_l, flanger_lpf_coef_q16);
    delayed_r = flanger_process_lpf_q16(delayed_r, &flanger_lpf_state_r, flanger_lpf_coef_q16);

    // Mix dry/wet
    int64_t mix_l = ((int64_t)*inout_l * (Q16_ONE - flanger_mix_q16) + (int64_t)delayed_l * flanger_mix_q16) >> 16;
    int64_t mix_r = ((int64_t)*inout_r * (Q16_ONE - flanger_mix_q16) + (int64_t)delayed_r * flanger_mix_q16) >> 16;

    mix_l = (mix_l * flanger_volume_q24) >> 24;
    mix_r = (mix_r * flanger_volume_q24) >> 24;

    *inout_l = clamp24((int32_t)mix_l);
    *inout_r = clamp24((int32_t)mix_r);

    flanger_write_pos = (flanger_write_pos + 1) % FLANGER_MAX_SAMPLES;
}

void flanger_process_block(int32_t* in_l, int32_t* in_r, size_t frames, FXmode mode) {
    for (size_t i = 0; i < frames; i++) {
        process_audio_flanger_sample(&in_l[i], &in_r[i], mode);
    }
    // LED (only update when selected)
    if (lfo_update_led_flag) {
        if (selectedEffects[selected_slot] == FLNG_EFFECT_INDEX) {
            // LED logic (left channel reference)
            lfo_led_state = (flanger_lfo_phase_l < 0x80000000);
            lfo_update_led_flag = false;
        }
    }
}

#endif // FLANGER_H
