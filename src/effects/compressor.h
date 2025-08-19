/* compressor.h
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

#ifndef COMPRESSOR_H
#define COMPRESSOR_H

#include <stdint.h>

// === Fixed-point compressor without floating point ===

#define Q24_ONE 0x01000000
#define SOFT_KNEE_WIDTH_Q24 float_to_q24(0.1f)  // ~10 dB knee width

// Compressor parameters (Q8.24 fixed-point)
static int32_t comp_threshold_q24 = 0;
static int32_t comp_inv_ratio_q24 = 0;
static int32_t comp_gain_q24 = Q24_ONE;
static int32_t comp_attack_a_q24 = 0;
static int32_t comp_release_a_q24 = 0;

static int32_t env_l_q24 = 0;
static int32_t env_r_q24 = 0;
static int32_t gain_l_q24 = Q24_ONE;
static int32_t gain_r_q24 = Q24_ONE;

// Compute compressor gain for a single channel
static inline int32_t compute_gain_q24(int32_t env_q24) {
    if (env_q24 <= 0 || comp_inv_ratio_q24 >= Q24_ONE)
        return Q24_ONE;

    const int32_t knee_half = SOFT_KNEE_WIDTH_Q24 >> 1;
    const int32_t knee_start = comp_threshold_q24 - knee_half;
    const int32_t knee_end   = comp_threshold_q24 + knee_half;

    if (env_q24 <= knee_start)
        return Q24_ONE;

    int32_t gain_end;
    {
        int32_t ratio_delta = Q24_ONE - comp_inv_ratio_q24;
        int32_t over_thresh = env_q24 - comp_threshold_q24;
        int32_t frac = qdiv(over_thresh, env_q24);
        if (frac > Q24_ONE) frac = Q24_ONE;
        gain_end = Q24_ONE - qmul(frac, ratio_delta);
    }

    if (env_q24 >= knee_end)
        return gain_end;

    uint32_t t_q16 = (uint32_t)(((int64_t)(env_q24 - knee_start) << 16) / SOFT_KNEE_WIDTH_Q24);
    return lerp_fixed(Q24_ONE, gain_end, t_q16);
}

// Initialize default compressor values
static inline void init_compressor(void) {
    env_l_q24 = env_r_q24 = 0;
    comp_threshold_q24 = float_to_q24(0.1f);  // ~ -20 dB
    comp_inv_ratio_q24 = float_to_q24(1.0f / 4.0f);
    comp_gain_q24 = Q24_ONE;
}

// Load pot values
static inline void load_compressor_parms_from_memory(void) {
    int pot;

    // Threshold: -20 dB to +20 dB
    pot = storedPotValue[COMP_EFFECT_INDEX][0];
    float thresh_db = -20.0f + ((float)pot / POT_MAX) * 40.0f;
    comp_threshold_q24 = db_to_q24(thresh_db);

    // Ratio: 1.1:1 to 20:1
    pot = storedPotValue[COMP_EFFECT_INDEX][1];
    float ratio = 1.1f + ((float)pot / POT_MAX) * 18.9f;
    comp_inv_ratio_q24 = float_to_q24(1.0f / ratio);

    // Attack time: 1 to 100 ms
    pot = storedPotValue[COMP_EFFECT_INDEX][2];
    float attack_ms = 1.0f + ((float)pot / POT_MAX) * 99.0f;
    comp_attack_a_q24 = ms_to_coeff_q24(attack_ms, 48000.0f);

    // Release time: 20 to 500 ms
    pot = storedPotValue[COMP_EFFECT_INDEX][3];
    float release_ms = 20.0f + ((float)pot / POT_MAX) * 480.0f;
    comp_release_a_q24 = ms_to_coeff_q24(release_ms, 48000.0f);

    // Makeup gain: 0 to +20 dB
    pot = storedPotValue[COMP_EFFECT_INDEX][5];
    float makeup_db = ((float)pot / POT_MAX) * 20.0f;
    comp_gain_q24 = db_to_q24(makeup_db);
}

static inline void update_compressor_params_from_pots(int changed_pot) {
    if (changed_pot < 0) return;
    storedPotValue[COMP_EFFECT_INDEX][changed_pot] = pot_value[changed_pot];
    load_compressor_parms_from_memory();
}

static inline void process_audio_compressor_sample(int32_t* inout_l, int32_t* inout_r) {
    for (int ch = 0; ch < 2; ++ch) {
        int32_t* x = (ch == 0) ? inout_l : inout_r;
        int32_t abs_x = (*x < 0) ? -*x : *x;
        int32_t* env_ptr = (ch == 0) ? &env_l_q24 : &env_r_q24;

        // Envelope follower
        if (abs_x > *env_ptr)
            *env_ptr = (int32_t)(((int64_t)*env_ptr * comp_attack_a_q24 + (int64_t)abs_x * (Q24_ONE - comp_attack_a_q24)) >> 24);
        else
            *env_ptr = (int32_t)(((int64_t)*env_ptr * comp_release_a_q24 + (int64_t)abs_x * (Q24_ONE - comp_release_a_q24)) >> 24);

        // Gain is computed less frequently in block processor
    }

    // Apply cached gain
    int64_t y_l = ((int64_t)(*inout_l) * gain_l_q24) >> 24;
    y_l = (y_l * comp_gain_q24) >> 24;
    *inout_l = clamp24((int32_t)y_l);

    int64_t y_r = ((int64_t)(*inout_r) * gain_r_q24) >> 24;
    y_r = (y_r * comp_gain_q24) >> 24;
    *inout_r = clamp24((int32_t)y_r);
}

void compressor_process_block(int32_t* in_l, int32_t* in_r, size_t frames) {
    for (size_t i = 0; i < frames; i++) {
        process_audio_compressor_sample(&in_l[i], &in_r[i]);

        // Update gain every N samples to reduce CPU
        static int counter = 0;
        if (++counter >= 4) {
            counter = 0;
            gain_l_q24 = compute_gain_q24(env_l_q24);
            gain_r_q24 = compute_gain_q24(env_r_q24);

            comp_linear_gain_q24_l = gain_l_q24;
            comp_linear_gain_q24_r = gain_r_q24;
        }
    }
}

#endif // COMPRESSOR_H