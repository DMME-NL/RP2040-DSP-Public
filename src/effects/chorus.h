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
#define MAX_CHORUS_DELAY_SAMPLES 256
#define Q16_ONE 0x00010000
#define Q24_ONE 0x01000000
#define CHORUS_MIN_DELAY_SAMPLES 16

// === Buffer ===
static int32_t chorus_buffer[MAX_CHORUS_DELAY_SAMPLES];
static uint32_t chorus_write_pos = 0;

// === Parameters ===
static uint32_t chorus_depth_q16 = Q16_ONE / 2;
static uint32_t chorus_speed_q16 = Q16_ONE / 4;
static uint32_t chorus_mix_q16   = Q16_ONE / 2;
static uint32_t chorus_volume_q24 = Q24_ONE;

// === LFO Phases ===
static uint32_t chorus_lfo_phase[3] = {0, 0x55555555, 0xAAAAAAAA};
static uint32_t chorus_lfo_inc = 0;

extern bool lfo_led_state;

// --- LPF states ---
static int32_t chorus_lpf_state_l = 0;
static int32_t chorus_lpf_state_r = 0;
static uint32_t chorus_lpf_coef_q16 = 0x4000; // default

// one global flag the UI can poke (no header needed)
volatile int8_t ui_chorus_mode_pending = -1;  // -1 = no change

static ChorusMode chorus_current_mode = STEREO_3;

// Call this exactly when the user changes the chorus mode.
// It preserves continuity by deriving new phases from the *current* base phase.
static inline void chorus_set_mode(ChorusMode cmode) {
    uint32_t base = chorus_lfo_phase[0];
    switch (cmode) {
        case STEREO_3: // 0°, +120°, +240°
            chorus_lfo_phase[1] = base + 0x55555555u;
            chorus_lfo_phase[2] = base + 0xAAAAAAAAu;
            break;
        case STEREO_2: // 0°, +180° (3rd tap unused)
            chorus_lfo_phase[1] = base + 0x80000000u;
            chorus_lfo_phase[2] = base; // unused, keep valid value
            break;
        case MONO:     // all the same phase (only first tap used)
        default:
            chorus_lfo_phase[1] = base;
            chorus_lfo_phase[2] = base;
            break;
    }
}

static inline void chorus_apply_pending_mode_if_any(void) {
    int8_t pm = ui_chorus_mode_pending; // single volatile read
    if (pm >= 0 && pm < NUM_CHORUS_MODES && (ChorusMode)pm != chorus_current_mode) {
        chorus_current_mode = (ChorusMode)pm;
        chorus_set_mode(chorus_current_mode);  // your phase-offset setter
        // optional: ui_chorus_mode_pending = -1;  // not required, compare guards repeats
    }
}



static inline int32_t chorus_process_lpf_q16(int32_t x, int32_t *state, uint32_t coef_q16) {
    int32_t y = ((int64_t)(Q16_ONE - coef_q16) * x + (int64_t)coef_q16 * (*state)) >> 16;
    *state = y;
    return y;
}

// === Cubic Lagrange interpolation (fixed-point) ===
static inline int32_t chorus_lagrange_cubic_q16(int32_t y_minus1, int32_t y0, int32_t y1, int32_t y2, uint32_t frac_q16) {
    int64_t t  = frac_q16;
    int64_t t2 = (t * t) >> 16;
    int64_t t3 = (t2 * t) >> 16;

    int64_t a0 = (-t3 + (2 * t2) - t) >> 1;
    int64_t a1 = (3 * t3 - 5 * t2 + (2 * Q16_ONE)) >> 1;
    int64_t a2 = (-3 * t3 + 4 * t2 + t) >> 1;
    int64_t a3 = (t3 - t2) >> 1;

    int64_t result = 0;
    result += (a0 * y_minus1) >> 16;
    result += (a1 * y0)      >> 16;
    result += (a2 * y1)      >> 16;
    result += (a3 * y2)      >> 16;

    // return clamp24((int32_t)result);
    return result;
}

// === All-pass filter for smoothing ===
static int32_t chorus_ap_state_l = 0;
static int32_t chorus_ap_state_r = 0;
static uint32_t chorus_ap_coef_q16 = 0x8000; // ~0.5

static inline int32_t chorus_process_allpass_q16(int32_t x, int32_t *state, uint32_t coef_q16) {
    int32_t y = *state + ((int64_t)coef_q16 * (x - *state) >> 16);
    *state = y + ((int64_t)coef_q16 * (x - y) >> 16);
    return y;
}

// === Init ===
static inline void init_chorus(void) {
    memset(chorus_buffer, 0, sizeof(chorus_buffer));
    chorus_write_pos = 0;
    chorus_lfo_phase[0] = 0;
    chorus_lfo_phase[1] = 0x55555555;
    chorus_lfo_phase[2] = 0xAAAAAAAA;
    chorus_ap_state_l = 0;
    chorus_ap_state_r = 0;
    chorus_lpf_state_l = 0;
    chorus_lpf_state_r = 0;
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

    // Volume: 0.1 to 4.0
    pot = storedPotValue[CHRS_EFFECT_INDEX][5];
    chorus_volume_q24 = map_pot_to_q24(pot, float_to_q24(0.1f), float_to_q24(3.0f));
}

static inline void update_chorus_params_from_pots(int changed_pot) {
    if (changed_pot < 0 || changed_pot > 5) return;
    storedPotValue[CHRS_EFFECT_INDEX][changed_pot] = pot_value[changed_pot];
    load_chorus_parms_from_memory();
}

// === Process Sample ===
// Same structure as yours, but only computes the needed taps per mode.
static inline void process_audio_chorus_sample(int32_t* inout_l, int32_t* inout_r, ChorusMode cmode) {
    // advance all phases so their *offsets* stay fixed
    chorus_lfo_phase[0] += chorus_lfo_inc;
    chorus_lfo_phase[1] += chorus_lfo_inc;
    chorus_lfo_phase[2] += chorus_lfo_inc;    

    const uint32_t max_depth_samples = MAX_CHORUS_DELAY_SAMPLES - CHORUS_MIN_DELAY_SAMPLES - 4;

    // compute only what we need
    int32_t delayed0 = 0, delayed1 = 0, delayed2 = 0;

    // tap 0 (always used)
    {
        uint32_t lfo_val_q16 = lfo_q16_shape(chorus_lfo_phase[0], LFO_TRIANGLE);
        uint32_t scaled_q16  = (lfo_val_q16 * chorus_depth_q16) >> 16;

        uint32_t delay_samples = CHORUS_MIN_DELAY_SAMPLES + ((max_depth_samples * scaled_q16) >> 16);
        uint32_t int_delay = delay_samples;
        uint32_t frac_q16  = (delay_samples << 16) & 0xFFFF;

        uint32_t base  = (chorus_write_pos + MAX_CHORUS_DELAY_SAMPLES - int_delay - 1) % MAX_CHORUS_DELAY_SAMPLES;
        uint32_t prev  = (base - 1 + MAX_CHORUS_DELAY_SAMPLES) % MAX_CHORUS_DELAY_SAMPLES;
        uint32_t next  = (base + 1) % MAX_CHORUS_DELAY_SAMPLES;
        uint32_t next2 = (base + 2) % MAX_CHORUS_DELAY_SAMPLES;

        delayed0 = chorus_lagrange_cubic_q16(chorus_buffer[prev], chorus_buffer[base],
                                             chorus_buffer[next], chorus_buffer[next2], frac_q16);
    }

    if (cmode != MONO) {
        // tap 1 (stereo modes)
        uint32_t lfo_val_q16 = lfo_q16_shape(chorus_lfo_phase[1], LFO_TRIANGLE);
        uint32_t scaled_q16  = (lfo_val_q16 * chorus_depth_q16) >> 16;

        uint32_t delay_samples = CHORUS_MIN_DELAY_SAMPLES + ((max_depth_samples * scaled_q16) >> 16);
        uint32_t int_delay = delay_samples;
        uint32_t frac_q16  = (delay_samples << 16) & 0xFFFF;

        uint32_t base  = (chorus_write_pos + MAX_CHORUS_DELAY_SAMPLES - int_delay - 1) % MAX_CHORUS_DELAY_SAMPLES;
        uint32_t prev  = (base - 1 + MAX_CHORUS_DELAY_SAMPLES) % MAX_CHORUS_DELAY_SAMPLES;
        uint32_t next  = (base + 1) % MAX_CHORUS_DELAY_SAMPLES;
        uint32_t next2 = (base + 2) % MAX_CHORUS_DELAY_SAMPLES;

        delayed1 = chorus_lagrange_cubic_q16(chorus_buffer[prev], chorus_buffer[base],
                                             chorus_buffer[next], chorus_buffer[next2], frac_q16);

        if (cmode == STEREO_3) {
            // tap 2 (only stereo-3)
            lfo_val_q16 = lfo_q16_shape(chorus_lfo_phase[2], LFO_TRIANGLE);
            scaled_q16  = (lfo_val_q16 * chorus_depth_q16) >> 16;

            delay_samples = CHORUS_MIN_DELAY_SAMPLES + ((max_depth_samples * scaled_q16) >> 16);
            int_delay = delay_samples;
            frac_q16  = (delay_samples << 16) & 0xFFFF;

            base  = (chorus_write_pos + MAX_CHORUS_DELAY_SAMPLES - int_delay - 1) % MAX_CHORUS_DELAY_SAMPLES;
            prev  = (base - 1 + MAX_CHORUS_DELAY_SAMPLES) % MAX_CHORUS_DELAY_SAMPLES;
            next  = (base + 1) % MAX_CHORUS_DELAY_SAMPLES;
            next2 = (base + 2) % MAX_CHORUS_DELAY_SAMPLES;

            delayed2 = chorus_lagrange_cubic_q16(chorus_buffer[prev], chorus_buffer[base],
                                                 chorus_buffer[next], chorus_buffer[next2], frac_q16);
        }
    }

    // write mono input into buffer
    int32_t mono_in = (*inout_l >> 1) + (*inout_r >> 1);
    chorus_buffer[chorus_write_pos] = mono_in;
    chorus_write_pos = (chorus_write_pos + 1) % MAX_CHORUS_DELAY_SAMPLES;

    // map taps to L/R
    int32_t left_tap, right_tap;
    if (cmode == MONO) {
        left_tap = right_tap = delayed0;
    } else if (cmode == STEREO_2) {
        left_tap  = delayed0; // 0°
        right_tap = delayed1; // 180°
    } else { // STEREO_3
        left_tap  = (delayed0 >> 1) + (delayed1 >> 1); // 0° + 120°
        right_tap = (delayed2 >> 1) + (delayed1 >> 1); // 240° + 120°
    }

    // smoothing
    left_tap  = chorus_process_allpass_q16(left_tap,  &chorus_ap_state_l, chorus_ap_coef_q16);
    right_tap = chorus_process_allpass_q16(right_tap, &chorus_ap_state_r, chorus_ap_coef_q16);

    left_tap  = chorus_process_lpf_q16(left_tap,  &chorus_lpf_state_l, chorus_lpf_coef_q16);
    right_tap = chorus_process_lpf_q16(right_tap, &chorus_lpf_state_r, chorus_lpf_coef_q16);

    // mix
    int64_t mix_l = ((int64_t)*inout_l * (Q16_ONE - chorus_mix_q16) + (int64_t)left_tap  * chorus_mix_q16) >> 16;
    int64_t mix_r = ((int64_t)*inout_r * (Q16_ONE - chorus_mix_q16) + (int64_t)right_tap * chorus_mix_q16) >> 16;

    mix_l = (mix_l * chorus_volume_q24) >> 24;
    mix_r = (mix_r * chorus_volume_q24) >> 24;

    *inout_l = clamp24((int32_t)mix_l);
    *inout_r = clamp24((int32_t)mix_r);
}

void chorus_process_block(int32_t* in_l, int32_t* in_r, size_t frames, FXmode mode) {
    // Check if mode has changed
    chorus_apply_pending_mode_if_any();
    for (size_t i = 0; i < frames; i++) {
        process_audio_chorus_sample(&in_l[i], &in_r[i], chorus_current_mode);
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
