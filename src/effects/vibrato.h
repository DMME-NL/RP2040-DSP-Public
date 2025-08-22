/* vibrato.h
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
#ifndef VIBRATO_H
#define VIBRATO_H

static uint32_t vibrato_depth_q16 = 0;
static uint32_t vibrato_speed_q16 = 0;

static float vibrato_lfo_phase = 0.0f;

static inline void init_vibrato(void) {
    vibrato_lfo_phase = 0.0f;
}

static inline void load_vibrato_parms_from_memory(void) {
    vibrato_depth_q16 = ((float)storedPotValue[VIBR_EFFECT_INDEX][0] / POT_MAX) * Q16_ONE;
    vibrato_speed_q16 = ((float)storedPotValue[VIBR_EFFECT_INDEX][1] / POT_MAX) * Q16_ONE;
}

static inline void update_vibrato_params_from_pots(int changed_pot) {
    if (changed_pot < 0) return;

    float norm = (float)pot_value[changed_pot] / POT_MAX;
    if (norm < 0.0f) norm = 0.0f;
    if (norm > 1.0f) norm = 1.0f;

    if (changed_pot == 0) {
        vibrato_depth_q16 = norm * Q16_ONE;
        storedPotValue[VIBR_EFFECT_INDEX][0] = pot_value[0];
    } else if (changed_pot == 1) {
        vibrato_speed_q16 = norm * Q16_ONE;
        storedPotValue[VIBR_EFFECT_INDEX][1] = pot_value[1];
    }
}

static inline void process_audio_vibrato_sample(int32_t* inout_l, int32_t* inout_r, FXmode mode) {
    float in_l = *inout_l / 8388608.0f;
    float in_r = *inout_r / 8388608.0f;

    float depth = (float)vibrato_depth_q16 / Q16_ONE * 5.0f; // max ~5 ms modulation
    float speed = (float)vibrato_speed_q16 / Q16_ONE * 5.0f; // max ~5 Hz

    vibrato_lfo_phase += speed / 48000.0f;
    if (vibrato_lfo_phase >= 1.0f)
        vibrato_lfo_phase -= 1.0f;

    float lfo = sinf(2.0f * M_PI * vibrato_lfo_phase);
    float pitch_mod = 1.0f + depth * lfo * 0.01f; // small pitch change factor

    in_l *= pitch_mod;
    in_r *= pitch_mod;

    *inout_l = (int32_t)(in_l * 8388608.0f);
    *inout_r = (int32_t)(in_r * 8388608.0f);
}

void vibrato_process_block(int32_t* in_l, int32_t* in_r, size_t frames, FXmode mode) {
    for (size_t i = 0; i < frames; i++) {
        process_audio_vibrato_sample(&in_l[i], &in_r[i], mode);
    }
}

#endif // VIBRATO_H
