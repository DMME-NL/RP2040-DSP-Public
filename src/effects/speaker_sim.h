/* speaker-sim.h
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

#ifndef SPEAKER_SIM_H
#define SPEAKER_SIM_H

// === Filter instances ===
static OnePole hpf0, lpf4, lpf5;
static BPFPair bpf1, bpf2, bpf3;
static int32_t cab_output_gain_q24 = Q24_ONE;

static inline void set_bpf_cutoffs(BPFPair* f, int32_t fc, int32_t bw) {
    int32_t fc_low = fc - bw / 2;
    int32_t fc_high = fc + bw / 2;

    // Clamp within valid audio range
    if (fc_low < 20) fc_low = 20;
    if (fc_high > SAMPLE_RATE / 2) fc_high = SAMPLE_RATE / 2;

    f->hpf.a_q24 = fc_to_q24(fc_low, SAMPLE_RATE);
    f->lpf.a_q24 = fc_to_q24(fc_high, SAMPLE_RATE);
}

// === Processing ===
static inline int32_t process_speaker_channel(int32_t x, int ch) {
    // Stage 0: HPF
    int32_t* hpf_state = (ch == 0) ? &hpf0.state_l : &hpf0.state_r;
    int32_t y = apply_1pole_hpf(x, hpf_state, hpf0.a_q24) >> 1; // Divide by 2 to reduce gain slightly

    // Parallel group
    int32_t p1 = apply_1pole_bpf(x, &bpf1, ch);
    int32_t p2 = apply_1pole_bpf(x, &bpf2, ch);
    int32_t p3 = apply_1pole_bpf(x, &bpf3, ch);
    y += ((p1 >> 1) + (p2 >> 1) + (p3 >> 1)) >> 1; // Average the three BPF outputs

    // LPF 5kHz with -2dB
    int32_t* lpf4_state = (ch == 0) ? &lpf4.state_l : &lpf4.state_r;
    y = apply_1pole_lpf(y, lpf4_state, lpf4.a_q24);

    // LPF 8kHz with +6dB
    int32_t* lpf5_state = (ch == 0) ? &lpf5.state_l : &lpf5.state_r;
    y = apply_1pole_lpf(y, lpf5_state, lpf5.a_q24);

    // Apply gain (2dB) to the output in Q24
    y = qmul(y, 0x1420000); // ~2dB gain in Q24

    // Output gain (controlled by pot)
    return clamp24(qmul(y, cab_output_gain_q24));
}

static inline void speaker_sim_process_sample(int32_t* inout_l, int32_t* inout_r) {
    *inout_l = process_speaker_channel(*inout_l, 0);
    *inout_r = process_speaker_channel(*inout_r, 1);
}

void speaker_sim_process_block(int32_t* in_l, int32_t* in_r, size_t frames) {
    for (size_t i = 0; i < frames; ++i)
        speaker_sim_process_sample(&in_l[i], &in_r[i]);
}

// === Initialization ===
static inline void init_speaker_sim(void) {
    hpf0.a_q24 = fc_to_q24(80, SAMPLE_RATE);

    set_bpf_cutoffs(&bpf1, 120, 80);    // Fc = 120, BW = 80 → 80–160 Hz
    bpf1.gain_q24 = db_to_q24(5.0f);

    set_bpf_cutoffs(&bpf2, 600, 500);   // Fc = 600, BW = 500 → 375–825 Hz
    bpf2.gain_q24 = db_to_q24(-4.0f);

    set_bpf_cutoffs(&bpf3, 2500, 1200); // Fc = 2500, BW = 1200 → 1900–3100 Hz
    bpf3.gain_q24 = db_to_q24(6.0f);

    lpf4.a_q24 = fc_to_q24(5000, SAMPLE_RATE);
    lpf5.a_q24 = fc_to_q24(8000, SAMPLE_RATE);

    cab_output_gain_q24 = Q24_ONE;
}

static inline void load_speaker_sim_parms_from_memory(void) {
    int32_t pot;

    // === Pot 0: Low Cut HPF (30–200 Hz) ===
    pot = storedPotValue[CAB_SIM_EFFECT_INDEX][0];
    int32_t hpf_freq = map_pot_to_int(pot, 200, 30);  // Hz
    hpf0.a_q24 = fc_to_q24(hpf_freq, SAMPLE_RATE);

    // === Pot 1: Body Gain (–6 dB to +12 dB) ===
    pot = storedPotValue[CAB_SIM_EFFECT_INDEX][1];
    int32_t body_gain_q24 = map_pot_to_q24(pot, db_to_q24(-6.0f), db_to_q24(12.0f));
    bpf1.gain_q24 = body_gain_q24;

    // === Pot 2: Mid Scoop (–14 dB to 3 dB) ===
    pot = storedPotValue[CAB_SIM_EFFECT_INDEX][2];
    int32_t mid_dip_q24 = map_pot_to_q24(pot, db_to_q24(-14.0f), db_to_q24(0.0f));
    bpf2.gain_q24 = mid_dip_q24;

    // === Pot 3: Presence Gain (–6 dB to +12 dB) ===
    pot = storedPotValue[CAB_SIM_EFFECT_INDEX][3];
    int32_t pres_gain_q24 = map_pot_to_q24(pot, db_to_q24(-6.0f), db_to_q24(12.0f));
    bpf3.gain_q24 = pres_gain_q24;

    // === Pot 4: Air Freq (LPF5) – 3kHz to 10kHz ===
    pot = storedPotValue[CAB_SIM_EFFECT_INDEX][4];
    int32_t air_freq = map_pot_to_int(pot, 3000, 10000);
    lpf5.a_q24 = fc_to_q24(air_freq, SAMPLE_RATE);

    // === Pot 5: Output Volume (0.1x to 2.0x linear gain) ===
    pot = storedPotValue[CAB_SIM_EFFECT_INDEX][5];
    cab_output_gain_q24 = map_pot_to_q24(pot, float_to_q24(0.1f), float_to_q24(2.0f));
}

static inline void update_speaker_sim_params_from_pots(int changed_pot) {
    if (changed_pot < 0 || changed_pot >= 6) return;
    storedPotValue[CAB_SIM_EFFECT_INDEX][changed_pot] = pot_value[changed_pot];
    load_speaker_sim_parms_from_memory();
}


#endif // SPEAKER_SIM_H
