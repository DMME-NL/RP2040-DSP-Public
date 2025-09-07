/* chorus.h
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
#ifndef CHORUS_H
#define CHORUS_H

#include <stdint.h>
#include <string.h>
#include <math.h>

// === Constants ===
#define MAX_CHORUS_DELAY_SAMPLES 512
#define Q16_ONE  0x00010000
#define Q24_ONE  0x01000000
#define CHORUS_MIN_DELAY_SAMPLES 16

// Extra guard so our interpolation can touch up to base+2 (and base+3 via crossfade)
#define CHORUS_GUARD 6
#define CHORUS_MIN_Q16   ((int32_t)(CHORUS_MIN_DELAY_SAMPLES << 16))
#define CHORUS_MAX_Q16   ((int32_t)((CHORUS_MIN_DELAY_SAMPLES + (MAX_CHORUS_DELAY_SAMPLES - CHORUS_MIN_DELAY_SAMPLES - CHORUS_GUARD) - 1) << 16) | 0xFFFF)


// === Buffers (separated to avoid L↔R bleed) ===
static int32_t chorus_buf_l[MAX_CHORUS_DELAY_SAMPLES];
static int32_t chorus_buf_r[MAX_CHORUS_DELAY_SAMPLES];
static int32_t chorus_buf_c[MAX_CHORUS_DELAY_SAMPLES];  // center (mono) used only in 120° mode
static uint32_t chorus_write_pos = 0;

// === Parameters ===
static uint32_t chorus_depth_q16  = Q16_ONE / 2;
static uint32_t chorus_mix_q16    = Q16_ONE / 2;
static uint32_t chorus_volume_q24 = Q24_ONE;

// === LFO Phases ===
static uint32_t chorus_lfo_phase[3] = {0, 0x55555555, 0xAAAAAAAA}; // 0°, 120°, 240°
static uint32_t chorus_lfo_inc = 0;

extern bool lfo_led_state;

// --- LPF states ---
static int32_t chorus_lpf_state_l = 0;
static int32_t chorus_lpf_state_r = 0;
static uint32_t chorus_lpf_coef_q16 = 0x4000; // default

// --- Control-rate dezipper for each tap (Q16.16 delay value) ---
static int32_t chorus_delay_q16_state[3] = {0,0,0};

// Thiran states per tap (left/right/center)
static int32_t thiran_y_prev[3] = {0,0,0};
static int32_t thiran_xn1_prev[3] = {0,0,0}; // previous x[n-1] if needed (kept for symmetry)


// Smoothing coefficient for the control filter (Q16)
// y = (1-a)*x + a*y_prev.  a close to 1.0 -> slower/smoother.
// Start around 0xF000 (~0.94). Raise for more smoothing, lower for more ‘chew’.
static uint32_t chorus_delay_smooth_coef_q16 = 0xF000;


// === Simple one-pole LPF (Q16) ===
static inline int32_t chorus_process_lpf_q16(int32_t x, int32_t *state, uint32_t coef_q16) {
    int32_t y = ((int64_t)(Q16_ONE - coef_q16) * x + (int64_t)coef_q16 * (*state)) >> 16;
    *state = y;
    return y;
}

// 1st-order Thiran all-pass interpolator
// Approximates a fractional delay of (1 - mu) samples with flat magnitude.
// Input: two adjacent samples x[n-1]=x0 (older), x[n]=x1 (newer), mu in [0,1).
// a = (1 - mu) / (1 + mu)
// y[n] = -a * y[n-1] + x[n-1] + a * x[n]
static inline int32_t chorus_read_thiran_q16(
    const int32_t *buf,
    uint32_t base_idx,     // index of x[n-1]
    uint32_t frac_q16,     // mu in Q16
    int tap_index          // 0,1,2
){
    // Fetch adjacent samples
    uint32_t i0 = base_idx;                       // x[n-1]
    uint32_t i1 = (base_idx + 1) % MAX_CHORUS_DELAY_SAMPLES; // x[n]

    int32_t x0 = buf[i0];
    int32_t x1 = buf[i1];

    // a = (1 - mu) / (1 + mu)  (Q16)
    uint32_t one = Q16_ONE;
    uint32_t num = (frac_q16 >= one) ? 0 : (one - frac_q16);    // guard
    uint32_t den = (uint32_t)((uint64_t)one + (uint64_t)frac_q16);
    // Q16 division (32/32 -> 32), safe on RP2040
    uint32_t a_q16 = (den ? (uint32_t)(((uint64_t)num << 16) / den) : 0);

    // y = -a*y_prev + x0 + a*x1
    int64_t acc = 0;
    acc += x0;
    acc += ((int64_t)a_q16 * x1) >> 16;
    acc -= ((int64_t)a_q16 * thiran_y_prev[tap_index]) >> 16;

    int32_t y = (int32_t)acc;
    thiran_y_prev[tap_index] = y;
    thiran_xn1_prev[tap_index] = x0; // not strictly required, but handy if you extend to 2nd/3rd order
    return y;
}

// === Read a modulated tap with control smoothing + two-cubic crossfade ===
static inline int32_t chorus_read_tap_q16_smoothed(
    const int32_t *buf,
    uint32_t phase,
    uint32_t depth_q16,
    int tap_index // 0,1,2 selects smoothing state
){
    const uint32_t max_depth = MAX_CHORUS_DELAY_SAMPLES - CHORUS_MIN_DELAY_SAMPLES - CHORUS_GUARD;

    // LFO in Q16 (0..65535)
    uint32_t lfo_q16 = lfo_q16_shape(phase, LFO_TRIANGLE);

    // Keep full precision: Q16 * Q16 = Q32 (no shift yet)
    uint64_t scaled_full_q32 = (uint64_t)lfo_q16 * (uint64_t)depth_q16; // 0..~(65535^2)

    // Map to samples in Q16.16 with a single final shift:
    // offset_q16 = (max_depth << 16) * scaled_full_q32 >> 32
    uint32_t offset_q16 = (uint32_t)((((uint64_t)max_depth << 16) * scaled_full_q32) >> 32);

    // Target delay in Q16.16
    uint32_t target_delay_q16 = (CHORUS_MIN_DELAY_SAMPLES << 16) + offset_q16;

    // after computing y (smoothed Q16.16):
    int32_t prev_q16 = chorus_delay_q16_state[tap_index];
    int32_t y = (int32_t)( ((int64_t)(Q16_ONE - chorus_delay_smooth_coef_q16) * (int64_t)target_delay_q16
                        + (int64_t)chorus_delay_smooth_coef_q16 * (int64_t)prev_q16) >> 16 );

    if (y < CHORUS_MIN_Q16) y = CHORUS_MIN_Q16;
    if (y > CHORUS_MAX_Q16) y = CHORUS_MAX_Q16;
    chorus_delay_q16_state[tap_index] = y;

    // Use the smoothed delay
    uint32_t delay_q16 = (uint32_t)y;
    uint32_t int_delay = delay_q16 >> 16;
    uint32_t frac_q16  = delay_q16 & 0xFFFF;

    // Base index one behind the write head (same convention as your code)
    uint32_t base = (chorus_write_pos + MAX_CHORUS_DELAY_SAMPLES - int_delay - 1) % MAX_CHORUS_DELAY_SAMPLES;

    // --- Thiran all-pass fractional delay (flat magnitude, smooth phase) ---
    return chorus_read_thiran_q16(buf, base, frac_q16, tap_index);
}

// === Init ===
static inline void init_chorus(void) {
    memset(chorus_buf_l, 0, sizeof(chorus_buf_l));
    memset(chorus_buf_r, 0, sizeof(chorus_buf_r));
    memset(chorus_buf_c, 0, sizeof(chorus_buf_c));
    chorus_write_pos = 0;

    chorus_lfo_phase[0] = 0;
    chorus_lfo_phase[1] = 0x55555555u; // +120°
    chorus_lfo_phase[2] = 0xAAAAAAAAu; // +240°

    chorus_lpf_state_l = 0;
    chorus_lpf_state_r = 0;

    chorus_delay_q16_state[0] = (CHORUS_MIN_DELAY_SAMPLES << 16);
    chorus_delay_q16_state[1] = (CHORUS_MIN_DELAY_SAMPLES << 16);
    chorus_delay_q16_state[2] = (CHORUS_MIN_DELAY_SAMPLES << 16);
}

// === Load Parameters ===
static inline void load_chorus_parms_from_memory(void) {
    int32_t pot;

    // Speed: 0.05 to 5 Hz
    pot = storedPotValue[CHRS_EFFECT_INDEX][0];
    float hz = 0.05f + ((float)pot / POT_MAX) * (5.0f - 0.05f);
    chorus_lfo_inc = (uint32_t)((hz / SAMPLE_RATE) * 4294967296.0f);

    // Depth: 0 to 1
    pot = storedPotValue[CHRS_EFFECT_INDEX][1];
    chorus_depth_q16 = map_pot_to_q16(pot, 0, Q16_ONE);

    // LPF cutoff: 100 Hz to 8 kHz (pot #4)
    pot = storedPotValue[CHRS_EFFECT_INDEX][4];
    float min_hz = 100.0f;
    float max_hz = 8000.0f;
    float norm = (float)pot / POT_MAX;
    float freq_hz = min_hz * powf(max_hz / min_hz, norm);

    float alpha = expf(-2.0f * 3.1415926f * freq_hz / SAMPLE_RATE);
    if (alpha < 0.0f) alpha = 0.0f;
    if (alpha > 1.0f) alpha = 1.0f;
    chorus_lpf_coef_q16 = float_to_q16(alpha);

    // Mix: 0 to 1
    pot = storedPotValue[CHRS_EFFECT_INDEX][3];
    chorus_mix_q16 = map_pot_to_q16(pot, 0, Q16_ONE);

    // Volume: 0.1 to 3.0
    pot = storedPotValue[CHRS_EFFECT_INDEX][5];
    chorus_volume_q24 = map_pot_to_q24(pot, float_to_q24(0.1f), float_to_q24(3.0f));
}

static inline void update_chorus_params_from_pots(int changed_pot) {
    if (changed_pot < 0 || changed_pot > 5) return;
    storedPotValue[CHRS_EFFECT_INDEX][changed_pot] = pot_value[changed_pot];
    load_chorus_parms_from_memory();
}

// === Process Sample ===
static inline void process_audio_chorus_sample(int32_t* inout_l, int32_t* inout_r, ChorusMode cmode) {
    // Advance LFOs
    chorus_lfo_phase[0] += chorus_lfo_inc;   // advance master phase

    switch (cmode) {
        case STEREO_2: // 0°, +180°
            chorus_lfo_phase[1] = chorus_lfo_phase[0] + 0x80000000u;
            break;

        case STEREO_3: // 0°, +120°, +240°
            chorus_lfo_phase[1] = chorus_lfo_phase[0] + 0x55555555u;
            chorus_lfo_phase[2] = chorus_lfo_phase[0] + 0xAAAAAAAAu;
            break;

        case MONO:
        default: // both channels same
            chorus_lfo_phase[1] = chorus_lfo_phase[0];
            chorus_lfo_phase[2] = chorus_lfo_phase[0];
            break;
    }

    // -------- READ FIRST (use current chorus_write_pos as the head) --------
    int32_t left_tap, right_tap;

    if (cmode == MONO) {
        // Each channel modulated the same *but* from its own buffer -> no crossfeed
        int32_t tL = chorus_read_tap_q16_smoothed(chorus_buf_l, chorus_lfo_phase[0], chorus_depth_q16, 0);
        int32_t tR = chorus_read_tap_q16_smoothed(chorus_buf_r, chorus_lfo_phase[0], chorus_depth_q16, 1);
        left_tap  = tL;
        right_tap = tR;
    } else if (cmode == STEREO_2) {
        // 0° on L buffer, 180° on R buffer -> no crossfeed, wide separation
        int32_t tL = chorus_read_tap_q16_smoothed(chorus_buf_l, chorus_lfo_phase[0], chorus_depth_q16, 0);
        int32_t tR = chorus_read_tap_q16_smoothed(chorus_buf_r, chorus_lfo_phase[1], chorus_depth_q16, 1);
        left_tap  = tL;
        right_tap = tR;
    } else { // STEREO_3  (0° L, +120° C, +240° R)
        int32_t tL = chorus_read_tap_q16_smoothed(chorus_buf_l, chorus_lfo_phase[0], chorus_depth_q16, 0);
        int32_t tC = chorus_read_tap_q16_smoothed(chorus_buf_c, chorus_lfo_phase[1], chorus_depth_q16, 1);
        int32_t tR = chorus_read_tap_q16_smoothed(chorus_buf_r, chorus_lfo_phase[2], chorus_depth_q16, 2);

        // Use center tap in both sides (your previous mapping)
        left_tap  = (tL >> 1) + (tC >> 1);
        right_tap = (tR >> 1) + (tC >> 1);
    }

    // -------- THEN WRITE current input into the rings --------
    chorus_buf_l[chorus_write_pos] = *inout_l;
    chorus_buf_r[chorus_write_pos] = *inout_r;
    chorus_buf_c[chorus_write_pos] = (*inout_l >> 1) + (*inout_r >> 1);

    // -------- NOW advance the write pointer --------
    chorus_write_pos = (chorus_write_pos + 1) % MAX_CHORUS_DELAY_SAMPLES;
   
    left_tap  = chorus_process_lpf_q16(left_tap,  &chorus_lpf_state_l, chorus_lpf_coef_q16);
    right_tap = chorus_process_lpf_q16(right_tap, &chorus_lpf_state_r, chorus_lpf_coef_q16);

    // Mix
    int64_t mix_l = ((int64_t)*inout_l * (Q16_ONE - chorus_mix_q16) + (int64_t)left_tap  * chorus_mix_q16) >> 16;
    int64_t mix_r = ((int64_t)*inout_r * (Q16_ONE - chorus_mix_q16) + (int64_t)right_tap * chorus_mix_q16) >> 16;

    mix_l = (mix_l * chorus_volume_q24) >> 24;
    mix_r = (mix_r * chorus_volume_q24) >> 24;

    *inout_l = clamp24((int32_t)mix_l);
    *inout_r = clamp24((int32_t)mix_r);
}

void chorus_process_block(int32_t* in_l, int32_t* in_r, size_t frames, FXmode mode) {
    for (size_t i = 0; i < frames; i++) {
        process_audio_chorus_sample(&in_l[i], &in_r[i], mode);
    }
    // LED (only update when selected)
    if (lfo_update_led_flag) {
        if (selectedEffects[selected_slot] == CHRS_EFFECT_INDEX) {
            lfo_led_state = (chorus_lfo_phase[0] < 0x80000000);
            lfo_update_led_flag = false;
        }
    }
}

#endif // CHORUS_H
