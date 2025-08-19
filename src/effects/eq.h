/* eq.h
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

#ifndef EQ_H
#define EQ_H

#include <stdint.h>

// --- equalizer parameters in Q8.24 ---
static int32_t eq_volume        = 0x01000000;
static int32_t eq_low_gain_q24  = 0x01000000;
static int32_t eq_mid_gain_q24  = 0x01000000;
static int32_t eq_mid_a_q24     = MID_A_Q24;
static int32_t eq_high_gain_q24 = 0x01000000;
static int32_t eq_lpf_a_q24     = LPF_A_Q24;


// --- Filter states ---
static int32_t eq_low_state_l = 0, eq_mid_lp_state_l = 0, eq_mid_hp_state_l = 0, eq_high_state_l = 0;
static int32_t eq_low_state_r = 0, eq_mid_lp_state_r = 0, eq_mid_hp_state_r = 0, eq_high_state_r = 0;
static int32_t eq_lpf_state_l = 0, eq_lpf_state_r = 0;
static int32_t eq_hpf_state_l = 0, eq_hpf_state_r = 0;

// --- Per-channel equalizer processing ---
static inline __attribute__((always_inline)) int32_t process_eq_channel(
    int32_t s,
    int32_t *low_state,
    int32_t *mid_lp_state,
    int32_t *mid_hp_state,
    int32_t *high_state,
    int32_t *lpf_state,
    int32_t *hpf_state
) {

    // Reduce input -12dB to prevent clipping 
    s = s >> 2;

    // Low-shelf
    int32_t low_out = apply_1pole_lpf(s, low_state, BASS_A_Q24); // Global BASS
    low_out = (int32_t)(((int64_t)low_out * eq_low_gain_q24) >> 24);

    // Mid band-pass
    int32_t mid_band = apply_1pole_lpf(
        apply_1pole_hpf(s, mid_hp_state, eq_mid_a_q24),
        mid_lp_state, eq_mid_a_q24
    );
    int32_t mid_out = (int32_t)(((int64_t)mid_band * eq_mid_gain_q24) >> 24);

    // High-shelf filter
    int32_t high_out = s - apply_1pole_lpf(s, high_state, TREBLE_A_Q24); // Global TREB
    high_out = (int32_t)(((int64_t)high_out * eq_high_gain_q24) >> 24);

    // Mix Tonestack
    int64_t y = low_out + mid_out + high_out;
    y = (y * eq_volume) >> 24;

    // LPF 
    y = apply_1pole_lpf(y, lpf_state, eq_lpf_a_q24);   // Global LPF

    int32_t output = clamp24((int32_t)y);
    return output;
}

// --- Process stereo sample ---
static inline void process_audio_eq_sample(int32_t* inout_l, int32_t* inout_r) {
    *inout_l = process_eq_channel(*inout_l, &eq_low_state_l, &eq_mid_lp_state_l, &eq_mid_hp_state_l, &eq_high_state_l, &eq_lpf_state_l, &eq_hpf_state_l);
    *inout_r = process_eq_channel(*inout_r, &eq_low_state_r, &eq_mid_lp_state_r, &eq_mid_hp_state_r, &eq_high_state_r, &eq_lpf_state_r, &eq_hpf_state_r);
}

// --- Load parameters ---
static inline void load_eq_parms_from_memory(void) {
    int32_t pot;

    // Bass from -12dB to +6dB
    pot = storedPotValue[EQ_EFFECT_INDEX][0];
    eq_low_gain_q24  = map_pot_to_q24(pot, float_to_q24(0.25f), float_to_q24(2.0f));

    // Mid from -12dB to +9.5dB
    pot = storedPotValue[EQ_EFFECT_INDEX][1];
    eq_mid_gain_q24  = map_pot_to_q24(pot, float_to_q24(0.25f), float_to_q24(3.0f));

    // Mid frequency: 300 Hz to 1 kHz
    pot = storedPotValue[EQ_EFFECT_INDEX][2];
    eq_mid_a_q24 = map_pot_to_q24(pot, fc_to_q24(300, SAMPLE_RATE), fc_to_q24(1000, SAMPLE_RATE));

    // Treb from -12dB to +6dB
    pot = storedPotValue[EQ_EFFECT_INDEX][3];
    eq_high_gain_q24 = map_pot_to_q24(pot, float_to_q24(0.25f), float_to_q24(2.0f));

    // LPF cutoff: 3 kHz to 16 kHz
    pot = storedPotValue[EQ_EFFECT_INDEX][4];
    eq_lpf_a_q24 = map_pot_to_q24(pot, fc_to_q24(3000, SAMPLE_RATE), fc_to_q24(16000, SAMPLE_RATE));

    // Volume from 0.1x to 6.0x
    pot = storedPotValue[EQ_EFFECT_INDEX][5];
    eq_volume        = map_pot_to_q24(pot, float_to_q24(0.1f), float_to_q24(8.0f));

    eq_low_state_l = eq_mid_lp_state_l = eq_mid_hp_state_l = eq_high_state_l = 0;
    eq_low_state_r = eq_mid_lp_state_r = eq_mid_hp_state_r = eq_high_state_r = 0;
    eq_lpf_state_l = eq_lpf_state_r = 0;
    eq_hpf_state_l = eq_hpf_state_r = 0;
}

// --- Update from UI ---
static inline void update_eq_params_from_pots(int changed_pot) {
    if (changed_pot < 0 || changed_pot > 5) return;
    storedPotValue[EQ_EFFECT_INDEX][changed_pot] = pot_value[changed_pot];
    load_eq_parms_from_memory();
}

void eq_process_block(int32_t* in_l, int32_t* in_r, size_t frames) {
    for (size_t i = 0; i < frames; i++) {
        process_audio_eq_sample(&in_l[i], &in_r[i]);
    }
}

#endif // equalizer_H
