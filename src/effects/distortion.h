/* distortion.h
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

#ifndef DISTORTION_H
#define DISTORTION_H

#include <stdint.h>

// --- distortion parameters in Q8.24 ---
static int32_t ds_gain          = 0x01000000;
static int32_t ds_volume        = 0x01000000;
static int32_t ds_low_gain_q24  = 0x01000000;
static int32_t ds_mid_gain_q24  = 0x01000000;
static int32_t ds_mid_a_q24     = MID_A_Q24;
static int32_t ds_high_gain_q24 = 0x01000000;
static int32_t ds_asym_q24      = 0x0119999A;   // Fixed at ~40%

// --- Filter states ---
static int32_t ds_low_state_l = 0, ds_mid_lp_state_l = 0, ds_mid_hp_state_l = 0, ds_high_state_l = 0;
static int32_t ds_low_state_r = 0, ds_mid_lp_state_r = 0, ds_mid_hp_state_r = 0, ds_high_state_r = 0;
static int32_t ds_lpf_state_l = 0, ds_lpf_state_r = 0;
static int32_t ds_hpf_state_l = 0, ds_hpf_state_r = 0;

// --- Fixed clip threshold and asymmetry ---
#define DS_CLIP_THRESH_Q24 0x400000 // ±0.25

// --- Clipping stage with asymmetry and soft knee ---
static inline int32_t diode_clip(int32_t x) {
    const int32_t knee = 0x040000; // 0.0625 (soft knee region)

    int32_t pos_thresh = DS_CLIP_THRESH_Q24;
    int32_t neg_thresh = -((int32_t)(((int64_t)DS_CLIP_THRESH_Q24 * ds_asym_q24) >> 24));

    if (x > pos_thresh + knee) {
        x = pos_thresh;
    } else if (x > pos_thresh) {
        x = pos_thresh - ((x - pos_thresh) >> 1);
    } else if (x < neg_thresh - knee) {
        x = neg_thresh;
    } else if (x < neg_thresh) {
        x = neg_thresh + ((neg_thresh - x) >> 1);
    }

    return x * 6; // Makeup gain
}

// --- Per-channel distortion processing ---
static inline __attribute__((always_inline)) int32_t process_ds_channel(
    int32_t s,
    int32_t *low_state,
    int32_t *mid_lp_state,
    int32_t *mid_hp_state,
    int32_t *high_state,
    int32_t *lpf_state,
    int32_t *hpf_state
) {
    s = (int32_t)(((int64_t)s * ds_gain) >> 24);

    // HPF before clipping to reduce rumble
    s = apply_1pole_hpf(s, hpf_state, HPF_A_Q24);   // Global HPF

    // Clipping
    s = diode_clip(s);

    // LPF after clipping to reduce fizz
    s = apply_1pole_lpf(s, lpf_state, LPF_A_Q24);   // Global LPF

    // Low-shelf
    int32_t low_out = apply_1pole_lpf(s, low_state, BASS_A_Q24); // Global BASS
    low_out = (int32_t)(((int64_t)low_out * ds_low_gain_q24) >> 24);

    // Mid band-pass
    int32_t mid_band = apply_1pole_lpf(
        apply_1pole_hpf(s, mid_hp_state, ds_mid_a_q24),
        mid_lp_state, ds_mid_a_q24
    );
    int32_t mid_out = (int32_t)(((int64_t)mid_band * ds_mid_gain_q24) >> 24);

    // High-shelf filter
    int32_t high_out = s - apply_1pole_lpf(s, high_state, TREBLE_A_Q24); // Global TREB
    high_out = (int32_t)(((int64_t)high_out * ds_high_gain_q24) >> 24);

    // Mix Tonestack
    int64_t y = low_out + mid_out + high_out;
    y = (y * ds_volume) >> 24;

    int32_t output = clamp24((int32_t)y);
    return output;
}

// --- Process stereo sample ---
static inline void process_audio_distortion_sample(int32_t* inout_l, int32_t* inout_r) {
    *inout_l = process_ds_channel(*inout_l, &ds_low_state_l, &ds_mid_lp_state_l, &ds_mid_hp_state_l, &ds_high_state_l, &ds_lpf_state_l, &ds_hpf_state_l);
    *inout_r = process_ds_channel(*inout_r, &ds_low_state_r, &ds_mid_lp_state_r, &ds_mid_hp_state_r, &ds_high_state_r, &ds_lpf_state_r, &ds_hpf_state_r);
}

// --- Load parameters ---
static inline void load_distortion_parms_from_memory(void) {
    int32_t pot;

    // Gain from -26dB to 0dB
    pot = storedPotValue[DS_EFFECT_INDEX][0];
    ds_gain          = map_pot_to_q24(pot, float_to_q24(0.05f), float_to_q24(1.0f));

    // Bass from -12dB to +6dB
    pot = storedPotValue[DS_EFFECT_INDEX][1];
    ds_low_gain_q24  = map_pot_to_q24(pot, float_to_q24(0.25f), float_to_q24(2.0f));

    // Mid from -12dB to +9.5dB
    pot = storedPotValue[DS_EFFECT_INDEX][2];
    ds_mid_gain_q24  = map_pot_to_q24(pot, float_to_q24(0.25f), float_to_q24(3.0f));

    // Mid frequency: 400 Hz to 1 kHz
    pot = storedPotValue[DS_EFFECT_INDEX][3];
    ds_mid_a_q24 = map_pot_to_q24(pot, 0x0009F15A, 0x001F68E3);

    // Treb from -12dB to +6dB
    pot = storedPotValue[DS_EFFECT_INDEX][4];
    ds_high_gain_q24 = map_pot_to_q24(pot, float_to_q24(0.25f), float_to_q24(2.0f));

    // Volume from -6dB to +28dB
    pot = storedPotValue[DS_EFFECT_INDEX][5];
    ds_volume        = map_pot_to_q24(pot, float_to_q24(0.5f), float_to_q24(26.0f));

    ds_low_state_l = ds_mid_lp_state_l = ds_mid_hp_state_l = ds_high_state_l = 0;
    ds_low_state_r = ds_mid_lp_state_r = ds_mid_hp_state_r = ds_high_state_r = 0;
    ds_lpf_state_l = ds_lpf_state_r = 0;
    ds_hpf_state_l = ds_hpf_state_r = 0;
}

// --- Update from UI ---
static inline void update_distortion_params_from_pots(int changed_pot) {
    if (changed_pot < 0 || changed_pot > 5) return;
    storedPotValue[DS_EFFECT_INDEX][changed_pot] = pot_value[changed_pot];
    load_distortion_parms_from_memory();
}

void distortion_process_block(int32_t* in_l, int32_t* in_r, size_t frames) {
    for (size_t i = 0; i < frames; i++) {
        process_audio_distortion_sample(&in_l[i], &in_r[i]);
    }
}

#endif // distortion_H
