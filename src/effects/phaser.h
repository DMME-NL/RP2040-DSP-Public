/* phaser.h
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

#ifndef PHASER_H
#define PHASER_H

#include <stdint.h>
#include <string.h>

// === Fixed-point constants ===
#define Q24_ONE 0x01000000
#define Q15_ONE 0x00008000
#define Q16_ONE 0x00010000

#define NUM_PHASER_STAGES 4

// === Depth limits ===
static int32_t phaser_low_a_q24;
static int32_t phaser_high_a_q24;

// === LFO Phase States ===
static uint32_t phaser_lfo_phase[2] = {0, 0x80000000}; // 180 deg apart
static uint32_t phaser_lfo_inc = 0;

// === Parameters (Q24) ===
static int32_t phaser_depth_q24    = Q24_ONE / 2;
static int32_t phaser_feedback_q24 = 0;
static int32_t phaser_mix_q24      = Q24_ONE / 2;
static int32_t phaser_volume_q24   = Q24_ONE; // default 1.0

// === Allpass State ===
typedef struct {
    int32_t z1;
} AllpassState;

static AllpassState phaser_left[NUM_PHASER_STAGES];
static AllpassState phaser_right[NUM_PHASER_STAGES];

// === Feedback state ===
static int32_t feedback_l = 0;
static int32_t feedback_r = 0;

extern bool lfo_led_state;

// === Allpass Process ===
static inline int32_t allpass_process(int32_t x, int32_t a_q24, AllpassState* state) {
    int32_t diff = x - state->z1;
    int32_t temp = state->z1 + (int32_t)(((int64_t)a_q24 * diff) >> 24);
    state->z1 = temp;
    return temp;
}

// === Map LFO to coefficient ===
static inline int32_t phaser_lfo_coef(uint32_t phase) {
    int32_t tri_val = lfo_q16_shape(phase, LFO_TRIANGLE_SMOOTH) << 8; // Q24

    // Linear interpolation between low and high
    int64_t sweep = (int64_t)phaser_low_a_q24 * (Q24_ONE - tri_val)
                  + (int64_t)phaser_high_a_q24 * tri_val;
    return (int32_t)(sweep >> 24); // Back to Q24
}


// === Update LFO phases ===
static inline void update_phaser_lfos(void) {
    phaser_lfo_phase[0] += phaser_lfo_inc;
    phaser_lfo_phase[1] += phaser_lfo_inc;
}

// === Process Stereo Sample ===
static inline void process_audio_phaser_sample(int32_t* inout_l, int32_t* inout_r, FXmode mode) {
    update_phaser_lfos();

    // Set mono | stereo coefficients
    int32_t coef_l = phaser_lfo_coef(phaser_lfo_phase[0]);
    int32_t coef_r = coef_l;
    if (mode == FX_MONO) {
        // Mono mode uses same coefficient for both channels
    } else {
        coef_r = phaser_lfo_coef(phaser_lfo_phase[1]);
    }

    // --- small internal headroom: -6 dB ---
    int32_t inL = *inout_l >> 1;
    int32_t inR = *inout_r >> 1;

    // Negative feedback
    int32_t x_l = inL - feedback_l;
    int32_t x_r = inR - feedback_r;

    for (int i = 0; i < NUM_PHASER_STAGES; ++i) {
        x_l = allpass_process(x_l, coef_l, &phaser_left[i]);
        x_r = allpass_process(x_r, coef_r, &phaser_right[i]);
    }

    feedback_l = (int32_t)(((int64_t)x_l * phaser_feedback_q24) >> 24);
    feedback_r = (int32_t)(((int64_t)x_r * phaser_feedback_q24) >> 24);

    int64_t dry_l = ((int64_t)*inout_l * (Q24_ONE - phaser_mix_q24)) >> 24;
    int64_t wet_l = ((int64_t)x_l * phaser_mix_q24) >> 24;
    int64_t dry_r = ((int64_t)*inout_r * (Q24_ONE - phaser_mix_q24)) >> 24;
    int64_t wet_r = ((int64_t)x_r * phaser_mix_q24) >> 24;

    int32_t mixed_l = (int32_t)(dry_l + wet_l);
    int32_t mixed_r = (int32_t)(dry_r + wet_r);

    *inout_l = clamp24((int32_t)(((int64_t)mixed_l * phaser_volume_q24) >> 24));
    *inout_r = clamp24((int64_t)(((int64_t)mixed_r * phaser_volume_q24) >> 24));
}

// === Initialize Phaser ===
static inline void init_phaser(void) {
    memset(phaser_left, 0, sizeof(phaser_left));
    memset(phaser_right, 0, sizeof(phaser_right));
    feedback_l = feedback_r = 0;
    phaser_lfo_phase[0] = 0;
    phaser_lfo_phase[1] = 0x80000000;
}

// === Load parameters ===
static inline void load_phaser_parms_from_memory(void) {
    int32_t pot;

    // LFO speed: 0.05 to 4.0 Hz
    pot = storedPotValue[PHSR_EFFECT_INDEX][0];
    float hz = 0.05f + ((float)pot / POT_MAX) * (4.0f - 0.05f);
    phaser_lfo_inc = (uint32_t)((hz / SAMPLE_RATE) * 4294967296.0f);

    // Low frequency: 100 Hz to 2000 Hz
    pot = storedPotValue[PHSR_EFFECT_INDEX][1];
    float low_f = map_pot_to_freq(pot, 100, 1000);
    phaser_low_a_q24 = fc_to_q24(low_f, 48000);

    // High frequency: 300 Hz to 6000 Hz
    pot = storedPotValue[PHSR_EFFECT_INDEX][2];
    float high_f = map_pot_to_freq(pot, 1500, 6000);
    phaser_high_a_q24 = fc_to_q24(high_f, 48000);

    // Ensure proper order
    if (phaser_high_a_q24 < phaser_low_a_q24) {
        int32_t tmp = phaser_high_a_q24;
        phaser_high_a_q24 = phaser_low_a_q24;
        phaser_low_a_q24 = tmp;
    }

    // Feedback: 0.0 to 0.95 with nonlinear curve
    pot = storedPotValue[PHSR_EFFECT_INDEX][3];
    int32_t norm_fb = (int32_t)(((int64_t)pot * Q24_ONE) / POT_MAX);     // Q24
    int64_t norm_fb_sq = ((int64_t)norm_fb * norm_fb) >> 24;            // Q24
    phaser_feedback_q24 = (int32_t)((norm_fb_sq * float_to_q24(0.95f)) >> 24);

    // Mix: 0.0 to 1.0
    pot = storedPotValue[PHSR_EFFECT_INDEX][4];
    phaser_mix_q24 = map_pot_to_q24(pot, 0, Q24_ONE);

    // Volume: 0.1 to 4.0
    pot = storedPotValue[PHSR_EFFECT_INDEX][5];
    phaser_volume_q24 = map_pot_to_q24(pot, float_to_q24(0.1f), float_to_q24(4.0f));
}

// === Update parameters from pots ===
static inline void update_phaser_params_from_pots(int changed_pot) {
    if (changed_pot < 0 || changed_pot > 5) return;
    storedPotValue[PHSR_EFFECT_INDEX][changed_pot] = pot_value[changed_pot];
    load_phaser_parms_from_memory();
}

void phaser_process_block(int32_t* in_l, int32_t* in_r, size_t frames, FXmode mode) {
    for (size_t i = 0; i < frames; i++) {
        process_audio_phaser_sample(&in_l[i], &in_r[i], mode);
    }
    // LED (only update when selected)
    if (lfo_update_led_flag) {
        if (selectedEffects[selected_slot] == PHSR_EFFECT_INDEX) {
            // LED logic (left channel reference)
            lfo_led_state = (phaser_lfo_phase[0] < 0x80000000);
            lfo_update_led_flag = false;
        }
    }
}

#endif // PHASER_H
