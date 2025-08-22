/* 
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

#ifndef VOX_PREAMP_H
#define VOX_PREAMP_H

#include <stdint.h>
#include <stdbool.h>
#include <math.h>

/*  Vox AC30 Top-Boost style preamp (Q8.24)
    Flow: In → InputPad → HPF → PreVol+Bright → V1A → Coupler HPF → V2A
          → gentle CF-ish squish → Tone Proxy → Cut/Presence shelf → Post LPF → Master

    Controls (same mapping as your Marshall/Fender):
      pot[0]=PreVol, pot[1]=Bass, pot[2]=Mid, pot[3]=Treble, pot[4]=Presence(=CUT here), pot[5]=Master

    Depends on your helpers: qmul, clamp24, float_to_q24, db_to_q24, alpha_from_hz,
      map_pot_to_q24, apply_1pole_lpf/hpf, storedPotValue[..].
*/

/* ========================== Compile-time knobs =========================== */
#ifndef VOX_ECO
#define VOX_ECO                 1        // 1 = skip final post LPF
#endif

#ifndef VOX_USE_CUT
#define VOX_USE_CUT             1        // 1 = Presence knob acts as Vox "CUT" (high cut)
#endif

#ifndef VOX_ECO_SHELF
#define VOX_ECO_SHELF           0        // 0 = dedicated shelf (default, best tone)
#endif

#ifndef VOX_USE_X5
#define VOX_USE_X5              1        // keep x^5 gating
#endif

#ifndef VOX_EFFECT_INDEX
#define VOX_EFFECT_INDEX        PREAMP_EFFECT_INDEX
#endif

#define VOX_INPUT_PAD_DB       (-6.0f)
#define VOX_STACK_MAKEUP_DB    (+15.0f)
#define VOX_PREVOL_MIN_DB      (-43.0f)
#define VOX_STAGEA_GAIN        (+9.5f)
#define VOX_STAGEB_GAIN        (+10.5f)

/* Stage coefficients (ear-tuned; chimier than Fender, cleaner than Marshall) */
#define VOX_K3A                 (0.22f)
#define VOX_K5A                 (0.07f)
#define VOX_K3B                 (0.30f)
#define VOX_K5B                 (0.09f)

/* Asymmetry (elastic but not harsh) */
#define VOX_ASYM_A_BASE         (0.78f)
#define VOX_ASYM_A_DEPTH        (0.00f)

#define VOX_ASYM_B_BASE         (0.68f)
#define VOX_ASYM_B_DEPTH        (0.07f)

/* x^5 engages above this magnitude */
#define VOX_WS_X5_ON            (0.09f)

/* Env for Stage-B asym */
#define VOX_ENVB_HZ             (10.0f)
#define VOX_ENV_DECIM           2

/* Pre-Vol behavior */
#define VOX_PREVOL_TAPER        (1.5f)
#define VOX_PREVOL_TOP_BOOST_DB (+1.0f)

/* Bright mix cap (Voxy jangle at low PreVol) */
#define VOX_BRIGHT_MAX_DB       (+8.0f)

/* ============================== Voicing ================================== */
typedef struct {
    float pre_hpf_Hz;
    float cpl1_hz;
    float cpl2_hz;

    float bright_hz_min;   // at high PreVol
    float bright_hz_max;   // at low PreVol

    float bass_hz;
    float mid_hz;
    float treble_hz;
    float cut_hz;          // shelf corner for CUT/Presence
    float post_lpf_Hz;

    float stageA_shape;
    float stageA_asym;
    float stageB_shape;
    float stageB_asym;
} vox_voice_t;

static const vox_voice_t VOX_VOICE = {
    .pre_hpf_Hz   = 22.0f,
    .cpl1_hz      = 8.0f,
    .cpl2_hz      = 35.0f,

    .bright_hz_min= 3000.0f,
    .bright_hz_max= 9000.0f,

    // Top-Boost-ish tone centers
    .bass_hz      = 90.0f,
    .mid_hz       = 1000.0f,
    .treble_hz    = 6500.0f,
    .cut_hz       = 1800.0f,   // Vox "cut" operates ~upper mids/treble
    .post_lpf_Hz  = 14000.0f,

    .stageA_shape = 0.18f, .stageA_asym = 1.08f,
    .stageB_shape = 0.72f, .stageB_asym = 1.32f,
};

/* ============================ Parameters/State ============================ */
static int32_t vox_prevol_q24        = 0x01000000; // pot[0]
static int32_t vox_master_q24        = 0x01000000; // pot[5]
static int32_t vox_bass_gain_q24     = 0x01000000; // pot[1]
static int32_t vox_mid_gain_q24      = 0x01000000; // pot[2]
static int32_t vox_treble_gain_q24   = 0x01000000; // pot[3]

#if VOX_USE_CUT
static int32_t vox_cut_gain_q24      = 0x01000000; // pot[4] mapped to 0..-10 dB (as linear 1..0.316)
#else
static int32_t vox_presence_gain_q24 = 0x01000000; // optional presence boost 0..+8 dB
#endif

static int32_t vox_input_pad_q24     = 0x01000000;
static int32_t vox_bright_mix_q24    = 0;          // +dB highs (0..+8 dB)
static int32_t vox_stack_makeup_q24  = 0x01000000;

static int32_t vox_stageA_gain_q24   = 0x01000000; // ~ +11.5..12 dB
static int32_t vox_stageB_gain_q24   = 0x01000000;

/* Triode shaper coeffs (Q24) */
static int32_t vox_stageA_k3_q24     = 0;
static int32_t vox_stageA_k5_q24     = 0;
static int32_t vox_stageB_k3_q24     = 0;
static int32_t vox_stageB_k5_q24     = 0;

/* CF-ish squish */
static int32_t vox_cf_amount_q24     = 0;

/* One-pole alphas (Q24) */
static int32_t vox_pre_hpf_a_q24     = 0;
static int32_t vox_cpl1_a_q24        = 0;
static int32_t vox_bright_a_q24      = 0;
static int32_t vox_cpl2_a_q24        = 0;
static int32_t vox_bass_a_q24        = 0;
static int32_t vox_mid_a_q24         = 0;
static int32_t vox_treble_a_q24      = 0;
static int32_t vox_cut_a_q24         = 0;  // shared for cut/pres shelf
static int32_t vox_post_lpf_a_q24    = 0;

/* Stage-B envelope alpha */
static int32_t vox_envB_a_q24        = 0;

/* States (stereo) */
static int32_t vox_pre_hpf_state_l=0, vox_pre_hpf_state_r=0;
static int32_t vox_cpl1_state_l=0,   vox_cpl1_state_r=0;
static int32_t vox_bright_state_l=0, vox_bright_state_r=0;
static int32_t vox_cpl2_state_l=0,   vox_cpl2_state_r=0;

static int32_t vox_bass_state_l=0,   vox_bass_state_r=0;
static int32_t vox_mid_lp_state_l=0, vox_mid_lp_state_r=0;
static int32_t vox_mid_hp_state_l=0, vox_mid_hp_state_r=0;
static int32_t vox_treble_state_l=0, vox_treble_state_r=0;

static int32_t vox_shelf_state_l=0,  vox_shelf_state_r=0; // for cut/pres
static int32_t vox_post_lpf_state_l=0, vox_post_lpf_state_r=0;

/* Stage-B env + decim */
static int32_t vox_envB_state_l=0, vox_envB_state_r=0;
static uint8_t vox_envB_decim_l=0, vox_envB_decim_r=0;

/* =============================== Nonlinears =============================== */
static inline __attribute__((always_inline))
int32_t vox_triode_ws_35_asym_fast(int32_t x,
                                   int32_t k3_pos_q24, int32_t k5_pos_q24,
                                   int32_t k3_neg_q24, int32_t k5_neg_q24)
{
    if (x >  0x01000000) x =  0x01000000;
    if (x < -0x01000000) x = -0x01000000;

    int64_t x2 = ((int64_t)x * x) >> 24;
    int64_t x3 = (x2 * x) >> 24;
#if VOX_USE_X5
    int64_t x5 = (x3 * x2) >> 24;
#endif

    const int32_t k3 = (x >= 0) ? k3_pos_q24 : k3_neg_q24;
    int32_t term3 = (int32_t)(((int64_t)k3 * x3) >> 24);

    int32_t y = x - term3;

#if VOX_USE_X5
    int32_t ax  = (x >= 0) ? x : -x;
    if (ax > float_to_q24(VOX_WS_X5_ON)) {
        const int32_t k5 = (x >= 0) ? k5_pos_q24 : k5_neg_q24;
        int32_t term5 = (int32_t)(((int64_t)k5 * x5) >> 24);
        y += term5;
    }
#endif

    if (y >  0x01000000) y =  0x01000000;
    if (y < -0x01000000) y = -0x01000000;
    return y;
}

static inline __attribute__((always_inline))
int32_t vox_cathode_squish(int32_t x, int32_t amount_q24){
    if (x > 0){
        int32_t x2   = (int32_t)(((int64_t)x * x) >> 24);
        int32_t comp = qmul(amount_q24, x2);
        return x - comp;
    } else {
        return qmul(x, float_to_q24(0.98f));
    }
}

/* =============================== Core process ============================ */
static inline __attribute__((always_inline)) int32_t process_vox_channel(
    int32_t s,
    // filters
    int32_t* pre_hpf_state,
    int32_t* cpl1_state, int32_t* bright_state,
    int32_t* cpl2_state,
    int32_t* bass_state, int32_t* mid_lp_state, int32_t* mid_hp_state, int32_t* treble_state,
    int32_t* shelf_state,
    int32_t* post_lpf_state,
    // env
    int32_t* envB_state, uint8_t* envB_decim
){
    // Input pad
    s = qmul(s, vox_input_pad_q24);

    // optional global HPF (kept off for CPU)
    // if (vox_pre_hpf_a_q24) s = apply_1pole_hpf(s, pre_hpf_state, vox_pre_hpf_a_q24);

    // Pre-vol input coupler
    s = apply_1pole_hpf(s, cpl1_state, vox_cpl1_a_q24);

    // Pre-Volume + Bright
    if (vox_bright_mix_q24){
        int32_t l = apply_1pole_lpf(s, bright_state, vox_bright_a_q24);
        int32_t h = s - l;
        int32_t base       = qmul(s, vox_prevol_q24);
        int32_t bright_add = qmul(qmul(h, vox_bright_mix_q24), vox_prevol_q24);
        s = base + bright_add;
    } else {
        s = qmul(s, vox_prevol_q24);
    }

    // ================= Stage A =================
    s = qmul(s, vox_stageA_gain_q24);

    int32_t asymA = float_to_q24(VOX_ASYM_A_BASE);
    int32_t k3A_neg = qmul(vox_stageA_k3_q24, asymA);
    int32_t k5A_neg = qmul(vox_stageA_k5_q24, asymA);
    s = vox_triode_ws_35_asym_fast(s,
            vox_stageA_k3_q24, vox_stageA_k5_q24,
            k3A_neg,           k5A_neg);

    // Coupler into Stage B
    s = apply_1pole_hpf(s, cpl2_state, vox_cpl2_a_q24);

    // ================= Stage B =================
    int32_t envB;
    if ( ((*envB_decim)++ & (VOX_ENV_DECIM-1)) == 0 ){
        int32_t s_abs = (s >= 0) ? s : -s;
        envB = apply_1pole_lpf(s_abs, envB_state, vox_envB_a_q24);
    } else {
        envB = *envB_state;
    }

    int32_t asymB = float_to_q24(VOX_ASYM_B_BASE);
    asymB += qmul(float_to_q24(VOX_ASYM_B_DEPTH), envB);

    int32_t k3B_neg = qmul(vox_stageB_k3_q24, asymB);
    int32_t k5B_neg = qmul(vox_stageB_k5_q24, asymB);

    s = qmul(s, vox_stageB_gain_q24);
    s = vox_triode_ws_35_asym_fast(s,
            vox_stageB_k3_q24, vox_stageB_k5_q24,
            k3B_neg,           k5B_neg);

    // CF-ish feel (moderate)
    s = vox_cathode_squish(s, vox_cf_amount_q24);

    // ================= Tone proxy =================
    int32_t low      = apply_1pole_lpf(s, bass_state,   vox_bass_a_q24);
    int32_t low_out  = qmul(low, vox_bass_gain_q24);

    int32_t mid_bp   = apply_1pole_lpf( apply_1pole_hpf(s, mid_hp_state, vox_mid_a_q24),
                                        mid_lp_state, vox_mid_a_q24 );
    int32_t mid_out  = qmul(mid_bp, vox_mid_gain_q24);

    int32_t high_cmp = s - apply_1pole_lpf(s, treble_state, vox_treble_a_q24);
    int32_t high_out = qmul(high_cmp, vox_treble_gain_q24);

    int32_t mix32 = (int32_t)((int64_t)low_out + (int64_t)mid_out + (int64_t)high_out);

    // stack makeup
    mix32 = qmul(mix32, vox_stack_makeup_q24);

    // Cut/Presence shelf (post-stack)
#if VOX_USE_CUT
    // CUT: subtract some of the high component. pot up => more cut.
    if (vox_cut_a_q24){
        int32_t highs = mix32 - apply_1pole_lpf(mix32, shelf_state, vox_cut_a_q24);
        // vox_cut_gain_q24 maps 0 dB..-10 dB => 1.0..0.316
        // subtract proportion (1 - gain): 0..0.684 of highs
        int32_t cut_amt = 0x01000000 - vox_cut_gain_q24;
        mix32 -= qmul(highs, cut_amt);
    }
#else
  #if !VOX_ECO_SHELF
    if (vox_cut_a_q24 && vox_presence_gain_q24 != 0x01000000){
        int32_t highs = mix32 - apply_1pole_lpf(mix32, shelf_state, vox_cut_a_q24);
        int32_t pres_delta = qmul(highs, vox_presence_gain_q24 - 0x01000000);
        mix32 += pres_delta;
    }
  #else
    if (vox_presence_gain_q24 != 0x01000000){
        // ECO shelf: reuse high_cmp from above (cheaper, less accurate)
        int32_t pres_delta = qmul(high_cmp, vox_presence_gain_q24 - 0x01000000);
        mix32 += pres_delta;
    }
  #endif
#endif

#if !VOX_ECO
    if (vox_post_lpf_a_q24) mix32 = apply_1pole_lpf(mix32, post_lpf_state, vox_post_lpf_a_q24);
#endif

    // Master
    mix32 = qmul(mix32, vox_master_q24);
    return clamp24(mix32);
}

/* =============================== Public API ============================== */
static inline void process_audio_vox_sample(int32_t* inout_l, int32_t* inout_r, bool stereo){
    *inout_l = process_vox_channel(*inout_l,
        &vox_pre_hpf_state_l, &vox_cpl1_state_l, &vox_bright_state_l, &vox_cpl2_state_l,
        &vox_bass_state_l, &vox_mid_lp_state_l, &vox_mid_hp_state_l, &vox_treble_state_l,
        &vox_shelf_state_l, &vox_post_lpf_state_l,
        &vox_envB_state_l, &vox_envB_decim_l);

    if(!stereo){
        *inout_r = *inout_l;
    } else {
        *inout_r = process_vox_channel(*inout_r,
            &vox_pre_hpf_state_r, &vox_cpl1_state_r, &vox_bright_state_r, &vox_cpl2_state_r,
            &vox_bass_state_r, &vox_mid_lp_state_r, &vox_mid_hp_state_r, &vox_treble_state_r,
            &vox_shelf_state_r, &vox_post_lpf_state_r,
            &vox_envB_state_r, &vox_envB_decim_r);
    }
}

static inline void vox_preamp_process_block(int32_t* in_l, int32_t* in_r, size_t frames, bool stereo){
    for (size_t i=0;i<frames;i++){
        process_audio_vox_sample(&in_l[i], &in_r[i], stereo);
    }
}

/* =============================== Param load ============================== */
static inline void load_vox_params_from_memory(void){
    // 1) Filter alphas
    vox_input_pad_q24  = db_to_q24(VOX_INPUT_PAD_DB);
    vox_pre_hpf_a_q24  = alpha_from_hz(VOX_VOICE.pre_hpf_Hz);
    vox_cpl1_a_q24     = alpha_from_hz(VOX_VOICE.cpl1_hz);
    vox_cpl2_a_q24     = alpha_from_hz(VOX_VOICE.cpl2_hz);
    vox_bass_a_q24     = alpha_from_hz(VOX_VOICE.bass_hz);
    vox_mid_a_q24      = alpha_from_hz(VOX_VOICE.mid_hz);
    vox_treble_a_q24   = alpha_from_hz(VOX_VOICE.treble_hz);
    vox_cut_a_q24      = alpha_from_hz(VOX_VOICE.cut_hz);

#if !VOX_ECO
    vox_post_lpf_a_q24 = alpha_from_hz(VOX_VOICE.post_lpf_Hz);
#else
    vox_post_lpf_a_q24 = 0;
#endif

    // Stage-B env LPF
    vox_envB_a_q24     = alpha_from_hz(VOX_ENVB_HZ);

    // 2) Stage linear gains
    vox_stageA_gain_q24 = db_to_q24(VOX_STAGEA_GAIN);
    vox_stageB_gain_q24 = db_to_q24(VOX_STAGEB_GAIN);
    vox_stack_makeup_q24= db_to_q24(VOX_STACK_MAKEUP_DB);

    // 3) Triode shaper coeffs
    vox_stageA_k3_q24 = float_to_q24(VOX_K3A);
    vox_stageA_k5_q24 = float_to_q24(VOX_K5A);
    vox_stageB_k3_q24 = float_to_q24(VOX_K3B);
    vox_stageB_k5_q24 = float_to_q24(VOX_K5B);

    // 4) CF squish amount (moderate)
    vox_cf_amount_q24 = float_to_q24(0.16f + 0.10f * (VOX_VOICE.stageB_asym - 1.2f));

    // 5) Pots → controls
    int32_t pot;

    // Pre-Volume (audio-ish taper with mild top boost)
    pot = storedPotValue[VOX_EFFECT_INDEX][0];
    float p = (float)pot / 4095.0f;
    float t = powf(p, VOX_PREVOL_TAPER);

    float prevol_db = VOX_PREVOL_MIN_DB + (0.0f - VOX_PREVOL_MIN_DB) * t;
    prevol_db += VOX_PREVOL_TOP_BOOST_DB * powf(p, 6.0f);
    vox_prevol_q24 = db_to_q24(prevol_db);

    // Bright mix: stronger at low pre-vol; cap at VOX_BRIGHT_MAX_DB
    int32_t prevol01 = float_to_q24(powf(p, VOX_PREVOL_TAPER));
    int32_t inv01    = 0x01000000 - prevol01;
    vox_bright_mix_q24 = qmul(inv01, db_to_q24(VOX_BRIGHT_MAX_DB) - 0x01000000);

    // Bright corner tracks pre-vol
    float bright_fc = VOX_VOICE.bright_hz_min +
                      (VOX_VOICE.bright_hz_max - VOX_VOICE.bright_hz_min) * (1.0f - p);
    vox_bright_a_q24 = alpha_from_hz(bright_fc);

    // Tone stack gains (same ranges)
    pot = storedPotValue[VOX_EFFECT_INDEX][1];
    vox_bass_gain_q24   = map_pot_to_q24(pot, db_to_q24(-12.0f), db_to_q24(+6.0f));
    pot = storedPotValue[VOX_EFFECT_INDEX][2];
    vox_mid_gain_q24    = map_pot_to_q24(pot, db_to_q24(-12.0f), db_to_q24(+12.0f));
    pot = storedPotValue[VOX_EFFECT_INDEX][3];
    vox_treble_gain_q24 = map_pot_to_q24(pot, db_to_q24(-12.0f), db_to_q24(+6.0f));

    // Presence knob → Vox CUT (0 dB .. -10 dB)
    pot = storedPotValue[VOX_EFFECT_INDEX][4];
#if VOX_USE_CUT
    vox_cut_gain_q24 = map_pot_to_q24(pot, db_to_q24(0.0f), db_to_q24(-10.0f));
#else
    vox_presence_gain_q24 = map_pot_to_q24(pot, db_to_q24(0.0f), db_to_q24(+8.0f));
#endif

    // Master: −3..+22 dB
    pot = storedPotValue[VOX_EFFECT_INDEX][5];
    vox_master_q24 = map_pot_to_q24(pot, db_to_q24(-3.0f), db_to_q24(+22.0f));

    // 6) Reset states (avoid zipper)
    vox_pre_hpf_state_l=vox_pre_hpf_state_r=0;
    vox_cpl1_state_l=vox_cpl1_state_r=0; vox_bright_state_l=vox_bright_state_r=0;
    vox_cpl2_state_l=vox_cpl2_state_r=0;

    vox_bass_state_l=vox_bass_state_r=0;
    vox_mid_lp_state_l=vox_mid_lp_state_r=0; vox_mid_hp_state_l=vox_mid_hp_state_r=0;
    vox_treble_state_l=vox_treble_state_r=0;

    vox_shelf_state_l=vox_shelf_state_r=0;
    vox_post_lpf_state_l=vox_post_lpf_state_r=0;

    vox_envB_state_l=vox_envB_state_r=0;
    vox_envB_decim_l=vox_envB_decim_r=0;
}

#endif // VOX_PREAMP_H
