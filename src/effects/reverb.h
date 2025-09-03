/* reverb.h
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
#ifndef REVERB_H
#define REVERB_H

#include <stdint.h>
#include <string.h>

// === Reverb parameters (Q8.24) ===
static int32_t reverb_comb_feedback_q24    = 0x00A00000; // for comb filters (~0.625)
static int32_t reverb_allpass_feedback_q24 = 0x00500000; // for all-pass (~0.3125)
static int32_t reverb_mix_q24              = 0x00800000; // 0.5
static int32_t reverb_damping_q24          = 0x00800000; // ~0.5
static int32_t reverb_output_gain_q24 = Q24_ONE;
static int32_t reverb_wet_gain_q24    = Q24_ONE;
static int32_t reverb_dry_gain_q24    = Q24_ONE;

// === Comb delays (base sizes) ===
#define COMB1_SIZE_L 1597
#define COMB2_SIZE_L 1499
#define COMB3_SIZE_L 1423
#define COMB4_SIZE_L 1301
#define COMB5_SIZE_L 1187

#define COMB1_SIZE_R 1613
#define COMB2_SIZE_R 1483
#define COMB3_SIZE_R 1409
#define COMB4_SIZE_R 1289
#define COMB5_SIZE_R 1213

// === All-pass delays ===
#define AP1_SIZE 929
#define AP2_SIZE 701
#define AP3_SIZE 499

// === Comb buffers and states ===
static int32_t comb_buf1_l[COMB1_SIZE_L] = {0};
static int32_t comb_buf2_l[COMB2_SIZE_L] = {0};
static int32_t comb_buf3_l[COMB3_SIZE_L] = {0};
static int32_t comb_buf4_l[COMB4_SIZE_L] = {0};
static int32_t comb_buf5_l[COMB5_SIZE_L] = {0};
static int32_t comb_buf1_r[COMB1_SIZE_R] = {0};
static int32_t comb_buf2_r[COMB2_SIZE_R] = {0};
static int32_t comb_buf3_r[COMB3_SIZE_R] = {0};
static int32_t comb_buf4_r[COMB4_SIZE_R] = {0};
static int32_t comb_buf5_r[COMB5_SIZE_R] = {0};

static uint32_t comb_idx_l[5] = {0};
static uint32_t comb_idx_r[5] = {0};
static int32_t comb_damp_state_l[5] = {0};
static int32_t comb_damp_state_r[5] = {0};

// Virtual sizes (adjustable room size)
static uint32_t comb_size_l_virtual[5] = {
    COMB1_SIZE_L, COMB2_SIZE_L, COMB3_SIZE_L, COMB4_SIZE_L, COMB5_SIZE_L
};
static uint32_t comb_size_r_virtual[5] = {
    COMB1_SIZE_R, COMB2_SIZE_R, COMB3_SIZE_R, COMB4_SIZE_R, COMB5_SIZE_R
};

// === All-pass buffers and states ===
static int32_t ap_buf1_l[AP1_SIZE] = {0};
static int32_t ap_buf2_l[AP2_SIZE] = {0};
static int32_t ap_buf3_l[AP3_SIZE] = {0};
static int32_t ap_buf1_r[AP1_SIZE] = {0};
static int32_t ap_buf2_r[AP2_SIZE] = {0};
static int32_t ap_buf3_r[AP3_SIZE] = {0};

static uint32_t ap_idx_l[3] = {0};
static uint32_t ap_idx_r[3] = {0};

// Reusable pointer tables (no per-sample stack work)
static int32_t* comb_bufs_l_p[5] = { comb_buf1_l, comb_buf2_l, comb_buf3_l, comb_buf4_l, comb_buf5_l };
static int32_t* comb_bufs_r_p[5] = { comb_buf1_r, comb_buf2_r, comb_buf3_r, comb_buf4_r, comb_buf5_r };

static int32_t* ap_bufs_l_p[3] = { ap_buf1_l, ap_buf2_l, ap_buf3_l };
static int32_t* ap_bufs_r_p[3] = { ap_buf1_r, ap_buf2_r, ap_buf3_r };

static uint32_t ap_sizes_p[3] = { AP1_SIZE, AP2_SIZE, AP3_SIZE };

// === Comb filter with damping ===
static inline int32_t process_comb_damped(int32_t in, int32_t *buf, uint32_t size, uint32_t *idx, int32_t *damp_state) {
    int32_t delayed = buf[*idx];

    *damp_state += ((int64_t)(delayed - *damp_state) * reverb_damping_q24) >> 24;
    int32_t damped = *damp_state;

    int64_t fb = ((int64_t)damped * reverb_comb_feedback_q24) >> 24;
    int64_t sum = (int64_t)in + fb;

    buf[*idx] = (int32_t)sum;

    (*idx)++;
    if (*idx >= size) *idx = 0;

    return delayed;
}

// === All-pass filter ===
static inline int32_t process_reverb_allpass(int32_t in, int32_t *buf, uint32_t size, uint32_t *idx) {
    int32_t buf_out = buf[*idx];

    buf[*idx] = in + (int32_t)(((int64_t)buf_out * reverb_allpass_feedback_q24) >> 24);

    int32_t out = buf_out - (int32_t)(((int64_t)buf[*idx] * reverb_allpass_feedback_q24) >> 24);

    (*idx)++;
    if (*idx >= size) *idx = 0;

    return out;
}

// === Process one channel ===
static inline int32_t process_reverb(int32_t in,
                                     int32_t *comb_bufs[5], uint32_t comb_sizes[5], uint32_t comb_idxs[5], int32_t damp_states[5],
                                     int32_t *ap_bufs[3], uint32_t ap_sizes[3], uint32_t ap_idxs[3]) {
    int32_t comb_input = in >> 4;  // Reduce input energy
    int32_t comb_sum = 0;
    for (int i = 0; i < 5; i++) {
        comb_sum += process_comb_damped(comb_input, comb_bufs[i], comb_sizes[i], &comb_idxs[i], &damp_states[i]);
    }
    comb_sum >>= 2;

    int32_t ap_out = process_reverb_allpass(comb_sum, ap_bufs[0], ap_sizes[0], &ap_idxs[0]);
    ap_out = process_reverb_allpass(ap_out, ap_bufs[1], ap_sizes[1], &ap_idxs[1]);
    ap_out = process_reverb_allpass(ap_out, ap_bufs[2], ap_sizes[2], &ap_idxs[2]);
    
    //int32_t ap_out = comb_sum;

    int64_t wet = ((int64_t)ap_out * reverb_wet_gain_q24) >> 24;
    int64_t dry = ((int64_t)in * reverb_dry_gain_q24) >> 24;

    int64_t mix = (int64_t)(dry + wet) * reverb_output_gain_q24;
    int32_t out = clamp24((int32_t)(mix >> 24));

    return out;
}

// === Stereo wrapper ===
static inline void process_audio_reverb_sample(int32_t *inout_l, int32_t *inout_r) {
    *inout_l = process_reverb(*inout_l, comb_bufs_l_p, comb_size_l_virtual, comb_idx_l, comb_damp_state_l,
                              ap_bufs_l_p, ap_sizes_p, ap_idx_l);
    *inout_r = process_reverb(*inout_r, comb_bufs_r_p, comb_size_r_virtual, comb_idx_r, comb_damp_state_r,
                              ap_bufs_r_p, ap_sizes_p, ap_idx_r);
}

static inline void clear_reverb_memory(void) {
    // Clear comb buffers
    memset(comb_buf1_l, 0, sizeof(comb_buf1_l));
    memset(comb_buf2_l, 0, sizeof(comb_buf2_l));
    memset(comb_buf3_l, 0, sizeof(comb_buf3_l));
    memset(comb_buf4_l, 0, sizeof(comb_buf4_l));
    memset(comb_buf5_l, 0, sizeof(comb_buf5_l));
    memset(comb_buf1_r, 0, sizeof(comb_buf1_r));
    memset(comb_buf2_r, 0, sizeof(comb_buf2_r));
    memset(comb_buf3_r, 0, sizeof(comb_buf3_r));
    memset(comb_buf4_r, 0, sizeof(comb_buf4_r));
    memset(comb_buf5_r, 0, sizeof(comb_buf5_r));

    // Clear all-pass buffers
    memset(ap_buf1_l, 0, sizeof(ap_buf1_l));
    memset(ap_buf2_l, 0, sizeof(ap_buf2_l));
    memset(ap_buf3_l, 0, sizeof(ap_buf3_l));
    memset(ap_buf1_r, 0, sizeof(ap_buf1_r));
    memset(ap_buf2_r, 0, sizeof(ap_buf2_r));
    memset(ap_buf3_r, 0, sizeof(ap_buf3_r));

    // Clear damping states
    memset(comb_damp_state_l, 0, sizeof(comb_damp_state_l));
    memset(comb_damp_state_r, 0, sizeof(comb_damp_state_r));

    for (int i = 0; i < 5; i++) {
        comb_idx_l[i] = comb_idx_r[i] = 0;
    }
    for (int i = 0; i < 3; i++) {
        ap_idx_l[i] = ap_idx_r[i] = 0;
    }
}

// === Init ===
static inline void reverb_init(void) {
    clear_reverb_memory();
}

// === Load parameters ===
static inline void load_reverb_parms_from_memory(void) {
    int32_t pot;

    // Mix: 0 to 1
    pot = storedPotValue[REVB_EFFECT_INDEX][0];
    reverb_mix_q24 = map_pot_to_q24(pot, 0x00000000, Q24_ONE);

    // Decay (feedback): 0.80 to 0.95
    pot = storedPotValue[REVB_EFFECT_INDEX][1];
    reverb_comb_feedback_q24 = map_pot_to_q24(pot, float_to_q24(0.80f), float_to_q24(0.96f));

    // All-pass feedback (diffusion): 0.25 to 0.80
    pot = storedPotValue[REVB_EFFECT_INDEX][2];
    reverb_allpass_feedback_q24 = map_pot_to_q24(pot, float_to_q24(0.25f), float_to_q24(0.80));

    // Damping: 0.20 tp 0.90
    pot = storedPotValue[REVB_EFFECT_INDEX][3];
    reverb_damping_q24 = map_pot_to_q24(pot, float_to_q24(0.20f), float_to_q24(0.90f));

    // Room size scaling: 0.8 to 1.02 (clamp)
    pot = storedPotValue[REVB_EFFECT_INDEX][4];
    float room_scale = 0.52f + ((float)pot / POT_MAX) * 0.5f;  // 0.5 to 1.0

    // Update comb virtual lengths
    comb_size_l_virtual[0] = (uint32_t)(COMB1_SIZE_L * room_scale);
    comb_size_l_virtual[1] = (uint32_t)(COMB2_SIZE_L * room_scale);
    comb_size_l_virtual[2] = (uint32_t)(COMB3_SIZE_L * room_scale);
    comb_size_l_virtual[3] = (uint32_t)(COMB4_SIZE_L * room_scale);
    comb_size_l_virtual[4] = (uint32_t)(COMB5_SIZE_L * room_scale);

    comb_size_r_virtual[0] = (uint32_t)(COMB1_SIZE_R * room_scale);
    comb_size_r_virtual[1] = (uint32_t)(COMB2_SIZE_R * room_scale);
    comb_size_r_virtual[2] = (uint32_t)(COMB3_SIZE_R * room_scale);
    comb_size_r_virtual[3] = (uint32_t)(COMB4_SIZE_R * room_scale);
    comb_size_r_virtual[4] = (uint32_t)(COMB5_SIZE_R * room_scale);

    // Clamp low
    for (int i = 0; i < 5; i++) {
        if (comb_size_l_virtual[i] < 100) comb_size_l_virtual[i] = 100;
        if (comb_size_r_virtual[i] < 100) comb_size_r_virtual[i] = 100;
    }

    // Clamp high - individual limits
    if (comb_size_l_virtual[0] > COMB1_SIZE_L) comb_size_l_virtual[0] = COMB1_SIZE_L;
    if (comb_size_r_virtual[0] > COMB1_SIZE_R) comb_size_r_virtual[0] = COMB1_SIZE_R;
    if (comb_size_l_virtual[1] > COMB2_SIZE_L) comb_size_l_virtual[1] = COMB2_SIZE_L;
    if (comb_size_r_virtual[1] > COMB2_SIZE_R) comb_size_r_virtual[1] = COMB2_SIZE_R;
    if (comb_size_l_virtual[2] > COMB3_SIZE_L) comb_size_l_virtual[2] = COMB3_SIZE_L;
    if (comb_size_r_virtual[2] > COMB3_SIZE_R) comb_size_r_virtual[2] = COMB3_SIZE_R;    
    if (comb_size_l_virtual[3] > COMB4_SIZE_L) comb_size_l_virtual[3] = COMB4_SIZE_L;
    if (comb_size_r_virtual[3] > COMB4_SIZE_R) comb_size_r_virtual[3] = COMB4_SIZE_R;
    if (comb_size_l_virtual[4] > COMB5_SIZE_L) comb_size_l_virtual[4] = COMB5_SIZE_L;
    if (comb_size_r_virtual[4] > COMB5_SIZE_R) comb_size_r_virtual[4] = COMB5_SIZE_R;

    // Output gain: 0.1 to 4.0
    pot = storedPotValue[REVB_EFFECT_INDEX][5];
    reverb_output_gain_q24 = map_pot_to_q24(pot, float_to_q24(0.1f), float_to_q24(4.0f));

    reverb_wet_gain_q24 = (int32_t)(((int64_t)Q24_ONE * reverb_mix_q24) >> 24) << 2; // Wet gain is boosted
    reverb_dry_gain_q24 = Q24_ONE - reverb_mix_q24;
}

static inline void update_reverb_params_from_pots(int changed_pot) {
    if (changed_pot < 0) return;
    storedPotValue[REVB_EFFECT_INDEX][changed_pot] = pot_value[changed_pot];
    load_reverb_parms_from_memory();
}

void reverb_process_block(int32_t* in_l, int32_t* in_r, size_t frames) {
    for (size_t i = 0; i < frames; i++) {
        process_audio_reverb_sample(&in_l[i], &in_r[i]);
    }
}

#endif // REVERB_H

