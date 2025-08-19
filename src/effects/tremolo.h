/* tremolo.h
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

// ============================================================================
// === Audio Tremolo ==========================================================
// ============================================================================

// Tremolo effect processing function
static uint32_t tremolo_phase_q16 = 0;     // Q16 phase accumulator
static uint32_t tremolo_speed_q16 = 0;     // Q16 speed (LFO rate)
static uint32_t tremolo_depth_q16 = 0;     // Q16 depth (0 = no tremolo, 65536 = full depth)

// LFOs (Q16)
uint32_t lfo_l_q16 = 0;
uint32_t lfo_r_q16 = 0;

// ---- Tremolo: per-sample processing with mono/stereo LFO
void process_audio_tremolo_sample(int32_t* inout_l, int32_t* inout_r, FXmode mode) {
    // Derive phases
    uint32_t phase_l = tremolo_phase_q16;
    uint32_t phase_r = phase_l + (mode == FX_STEREO ? 0x80000000u : 0u); // 180° in stereo, same in mono

    // LFOs (Q16)
    lfo_l_q16 = lfo_q16_shape(phase_l, LFO_TRIANGLE);
    lfo_r_q16 = lfo_q16_shape(phase_r, LFO_TRIANGLE);

    // Depth mapping: amplitude = (1 - depth) + lfo * depth
    uint32_t one_minus_depth_q16 = Q16_ONE - tremolo_depth_q16;
    uint32_t amp_l_q16 = one_minus_depth_q16 + (uint32_t)(((uint64_t)lfo_l_q16 * tremolo_depth_q16) >> 16);
    uint32_t amp_r_q16 = one_minus_depth_q16 + (uint32_t)(((uint64_t)lfo_r_q16 * tremolo_depth_q16) >> 16);

    // Apply
    *inout_l = multiply_q16(*inout_l, amp_l_q16);
    *inout_r = multiply_q16(*inout_r, amp_r_q16);

    // Advance once
    tremolo_phase_q16 += tremolo_speed_q16;
}


void load_tremolo_parms_from_memory(void) {
    // Speed (simple linear; keep your scaling)
    uint16_t sp = storedPotValue[TREM_EFFECT_INDEX][0];
    tremolo_speed_q16 = (sp < 20) ? (20u * 250u) : ((uint32_t)sp * 250u);

    // Depth 0..1
    uint16_t dp = storedPotValue[TREM_EFFECT_INDEX][1];
    tremolo_depth_q16 = (dp < 20) ? 20u : ((uint32_t)dp * Q16_ONE) / POT_MAX;
}

void update_tremolo_params_from_pots(int changed_pot) {
    if (changed_pot < 0) return;

    if (changed_pot == 0) { // Speed
        uint16_t v = pot_value[0];
        tremolo_speed_q16 = (v < 20) ? (20u * 250u) : ((uint32_t)v * 250u);
        storedPotValue[TREM_EFFECT_INDEX][0] = v;
    } else if (changed_pot == 1) { // Depth
        uint16_t v = pot_value[1];
        tremolo_depth_q16 = (v < 20) ? 20u : ((uint32_t)v * Q16_ONE) / POT_MAX;
        storedPotValue[TREM_EFFECT_INDEX][1] = v;
    }
}

void tremolo_process_block(int32_t* in_l, int32_t* in_r, size_t frames, FXmode mode) {
    for (size_t i = 0; i < frames; i++) {
        process_audio_tremolo_sample(&in_l[i], &in_r[i], mode);
    }

    // LED (only update when selected)
    if (lfo_update_led_flag) {
        if (selectedEffects[selected_slot] == TREM_EFFECT_INDEX) {
            lfo_led_state = (lfo_l_q16 > 32768);
            lfo_update_led_flag = false;
        }
    }
}