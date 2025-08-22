
/* overdrive.h
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
 */#ifndef OVERDRIVE_H 
#define OVERDRIVE_H

#include <stdint.h>

// --- overdrive parameters in Q8.24 ---
static int32_t od_gain          = 0x01000000;
static int32_t od_volume        = 0x01000000;
static int32_t od_low_gain_q24  = 0x01000000;
static int32_t od_mid_gain_q24  = 0x01000000;
static int32_t od_mid_a_q24     = MID_A_Q24;
static int32_t od_high_gain_q24 = 0x01000000;
static int32_t od_asym_q24      = 0x018C28F6;   // Fixed at ~70%


// --- Filter states ---
static int32_t od_low_state_l = 0, od_mid_lp_state_l = 0, od_mid_hp_state_l = 0, od_high_state_l = 0;
static int32_t od_low_state_r = 0, od_mid_lp_state_r = 0, od_mid_hp_state_r = 0, od_high_state_r = 0;
static int32_t od_lpf_state_l = 0, od_lpf_state_r = 0;
static int32_t od_hpf_state_l = 0, od_hpf_state_r = 0;

// --- Approximate cubic soft clip with asymmetry ---
static inline __attribute__((always_inline)) int32_t soft_clip(int32_t x) {
    if (x > 0x01000000) x = 0x01000000;
    if (x < -0x01000000) x = -0x01000000;

    int32_t x2 = (x >> 12) * (x >> 12);
    int32_t x3 = (x2 >> 12) * (x >> 12);

    int32_t cubic;
    if (x >= 0) {
        cubic = x3 / 4;
    } else {
        int64_t temp = ((int64_t)x3 << 24) / (4 * (int64_t)od_asym_q24);
        cubic = (int32_t)temp;
    }

    return (x - cubic) * 3;
}

// --- Per-channel overdrive processing ---
static inline __attribute__((always_inline)) int32_t process_od_channel(
    int32_t s,
    int32_t *low_state,
    int32_t *mid_lp_state,
    int32_t *mid_hp_state,
    int32_t *high_state,
    int32_t *lpf_state,
    int32_t *hpf_state
) {
    s = (int32_t)(((int64_t)s * od_gain) >> 24);

    // HPF before clipping to reduce rumble
    s = apply_1pole_hpf(s, hpf_state, HPF_A_Q24);   // Global HPF

    // Clipping
    s = soft_clip(s);

    // LPF after clipping to reduce fizz
    s = apply_1pole_lpf(s, lpf_state, LPF_A_Q24);   // Global LPF

    // Low-shelf
    int32_t low_out = apply_1pole_lpf(s, low_state, BASS_A_Q24); // Global BASS
    low_out = (int32_t)(((int64_t)low_out * od_low_gain_q24) >> 24);

    // Mid band-pass
    int32_t mid_band = apply_1pole_lpf(
        apply_1pole_hpf(s, mid_hp_state, od_mid_a_q24),
        mid_lp_state, od_mid_a_q24
    );
    int32_t mid_out = (int32_t)(((int64_t)mid_band * od_mid_gain_q24) >> 24);

    // High-shelf filter
    int32_t high_out = s - apply_1pole_lpf(s, high_state, TREBLE_A_Q24); // Global TREB
    high_out = (int32_t)(((int64_t)high_out * od_high_gain_q24) >> 24);

    // Mix Tonestack
    int64_t y = low_out + mid_out + high_out;
    y = (y * od_volume) >> 24;

    int32_t output = clamp24((int32_t)y);
    return output;
}

// --- Process stereo sample ---
static inline void process_audio_overdrive_sample(int32_t* inout_l, int32_t* inout_r, bool stereo) {
    *inout_l = process_od_channel(*inout_l, &od_low_state_l, &od_mid_lp_state_l, &od_mid_hp_state_l, &od_high_state_l, &od_lpf_state_l, &od_hpf_state_l);
    if(!stereo){    *inout_r = *inout_l; } // Process MONO
    else{           *inout_r = process_od_channel(*inout_r, &od_low_state_r, &od_mid_lp_state_r, &od_mid_hp_state_r, &od_high_state_r, &od_lpf_state_r, &od_hpf_state_r);   }
}

// --- Load parameters ---
static inline void load_overdrive_parms_from_memory(void) {
    int32_t pot;

    // Gain from -26dB to 0dB
    pot = storedPotValue[OD_EFFECT_INDEX][0];
    od_gain          = map_pot_to_q24(pot, float_to_q24(0.05f), float_to_q24(1.0f));

    // Bass from -12dB to +6dB
    pot = storedPotValue[OD_EFFECT_INDEX][1];
    od_low_gain_q24  = map_pot_to_q24(pot, float_to_q24(0.25f), float_to_q24(2.0f));

    // Mid from -12dB to +9.5dB
    pot = storedPotValue[OD_EFFECT_INDEX][2];
    od_mid_gain_q24  = map_pot_to_q24(pot, float_to_q24(0.25f), float_to_q24(3.0f));

    // Mid frequency: 400 Hz to 1 kHz
    pot = storedPotValue[OD_EFFECT_INDEX][3];
    od_mid_a_q24 = map_pot_to_q24(pot, 0x0009F15A, 0x001F68E3);

    // Treb from -12dB to +6dB
    pot = storedPotValue[OD_EFFECT_INDEX][4];
    od_high_gain_q24 = map_pot_to_q24(pot, float_to_q24(0.25f), float_to_q24(2.0f));

    // Volume from -6dB to +26dB
    pot = storedPotValue[OD_EFFECT_INDEX][5];
    od_volume        = map_pot_to_q24(pot, float_to_q24(0.5f), float_to_q24(20.0f));

    od_low_state_l = od_mid_lp_state_l = od_mid_hp_state_l = od_high_state_l = 0;
    od_low_state_r = od_mid_lp_state_r = od_mid_hp_state_r = od_high_state_r = 0;
    od_lpf_state_l = od_lpf_state_r = 0;
    od_hpf_state_l = od_hpf_state_r = 0;
}

// --- Update from UI ---
static inline void update_overdrive_params_from_pots(int changed_pot) {
    if (changed_pot < 0 || changed_pot > 5) return;
    storedPotValue[OD_EFFECT_INDEX][changed_pot] = pot_value[changed_pot];
    load_overdrive_parms_from_memory();
}

void overdrive_process_block(int32_t* in_l, int32_t* in_r, size_t frames, bool stereo) {
    for (size_t i = 0; i < frames; i++) {
        process_audio_overdrive_sample(&in_l[i], &in_r[i], stereo);
    }
}

#endif // OVERDRIVE_H
