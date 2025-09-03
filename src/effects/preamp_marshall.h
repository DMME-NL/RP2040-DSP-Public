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
#ifndef MARSHALL_PREAMP_H
#define MARSHALL_PREAMP_H

/*  Marshall 2203/2204-style preamp (Q8.24)
    Flow: In → InputPad → HPF → PreVol+Bright → V1A → Coupler HPF → V2A (cold)
          → CF-ish squish → Tone Proxy → Presence shelf → Post LPF → Master
*/

/* ========================== Compile-time knobs =========================== */
#ifndef JCM_ECO
#define JCM_ECO                1        // 1 = skip final post LPF
#endif

#ifndef JCM_ECO_PRES
#define JCM_ECO_PRES           0        // 0 = use dedicated presence shelf (default)
#endif

#ifndef JCM_USE_X5
#define JCM_USE_X5             1        // keep x^5 gating
#endif

#define JCM_INPUT_PAD_DB      (-8.0f)
#define JCM_STACK_MAKEUP_DB   (+14.0f)
#define JCM_PREVOL_MIN_DB     (-40.0f)
#define JCM_STAGEA_GAIN       (+10.0f)
#define JCM_STAGEB_GAIN       (+12.0f)

#define JCM_K3A                (0.28f)
#define JCM_K5A                (0.08f)
#define JCM_K3B                (0.45f)
#define JCM_K5B                (0.15f)

#define JCM_ASYM_A_BASE        (0.70f)
#define JCM_ASYM_A_DEPTH       (0.00f)

#define JCM_ASYM_B_BASE        (0.62f)
#define JCM_ASYM_B_DEPTH       (0.08f)

#define JCM_WS_X5_ON           (0.08f)

#define JCM_ENVB_HZ            (12.0f)
#define JCM_ENV_DECIM          2

#define JCM_PREVOL_TAPER       (1.35f)
#define JCM_PREVOL_TOP_BOOST_DB (+2.0f)

#define JCM_BRIGHT_MAX_DB      (+4.0f)

/* ============================== Voicing ================================== */
typedef struct {
    float pre_hpf_Hz;
    float cpl1_hz;
    float cpl2_hz;
    float bright_hz_min;
    float bright_hz_max;
    float bass_hz;
    float mid_hz;
    float treble_hz;
    float presence_hz;
    float post_lpf_Hz;
    float stageA_shape, stageA_asym;
    float stageB_shape, stageB_asym;
} marshall_voice_t;

static const marshall_voice_t JCM_VOICE = {
    .pre_hpf_Hz   = 20.0f,
    .cpl1_hz      = 12.0f,
    .cpl2_hz      = 40.0f,
    .bright_hz_min= 2500.0f,
    .bright_hz_max= 8000.0f,
    .bass_hz      = 100.0f,
    .mid_hz       = 650.0f,
    .treble_hz    = 4500.0f,
    .presence_hz  = 3500.0f,
    .post_lpf_Hz  = 12000.0f,
    .stageA_shape = 0.20f, .stageA_asym = 1.12f,
    .stageB_shape = 0.85f, .stageB_asym = 1.45f,
};

/* ============================ Parameters/State ============================ */
static int32_t jcm_prevol_q24        = 0x01000000; // pot[0]
static int32_t jcm_master_q24        = 0x01000000; // pot[5]
static int32_t jcm_bass_gain_q24     = 0x01000000; // pot[1]
static int32_t jcm_mid_gain_q24      = 0x01000000; // pot[2]
static int32_t jcm_treble_gain_q24   = 0x01000000; // pot[3]
static int32_t jcm_presence_gain_q24 = 0x01000000; // pot[4]

static int32_t jcm_input_pad_q24     = 0x01000000;
static int32_t jcm_bright_mix_q24    = 0;
static int32_t jcm_stack_makeup_q24  = 0x01000000;

static int32_t jcm_stageA_gain_q24   = 0x01000000;
static int32_t jcm_stageB_gain_q24   = 0x01000000;

static int32_t jcm_stageA_k3_q24     = 0;
static int32_t jcm_stageA_k5_q24     = 0;
static int32_t jcm_stageB_k3_q24     = 0;
static int32_t jcm_stageB_k5_q24     = 0;

static int32_t jcm_cf_amount_q24     = 0;

static int32_t jcm_pre_hpf_a_q24     = 0;
static int32_t jcm_cpl1_a_q24        = 0;
static int32_t jcm_bright_a_q24      = 0;
static int32_t jcm_cpl2_a_q24        = 0;
static int32_t jcm_bass_a_q24        = 0;
static int32_t jcm_mid_a_q24         = 0;
static int32_t jcm_treble_a_q24      = 0;
static int32_t jcm_presence_a_q24    = 0;
static int32_t jcm_post_lpf_a_q24    = 0;

static int32_t jcm_envB_a_q24        = 0;

static int32_t jcm_pre_hpf_state_l=0, jcm_pre_hpf_state_r=0;
static int32_t jcm_cpl1_state_l=0, jcm_cpl1_state_r=0;
static int32_t jcm_bright_state_l=0, jcm_bright_state_r=0;
static int32_t jcm_cpl2_state_l=0, jcm_cpl2_state_r=0;
static int32_t jcm_bass_state_l=0, jcm_bass_state_r=0;
static int32_t jcm_mid_lp_state_l=0, jcm_mid_lp_state_r=0;
static int32_t jcm_mid_hp_state_l=0, jcm_mid_hp_state_r=0;
static int32_t jcm_treble_state_l=0, jcm_treble_state_r=0;
static int32_t jcm_presence_state_l=0, jcm_presence_state_r=0;
static int32_t jcm_post_lpf_state_l=0, jcm_post_lpf_state_r=0;

static int32_t jcm_envB_state_l=0, jcm_envB_state_r=0;
static uint8_t jcm_envB_decim_l=0, jcm_envB_decim_r=0;

/* --- Cached constants (non-RT) */
static int32_t jcm_ws_x5_on_q24, jcm_cf_recover_q24;
static int32_t jcm_k3A_neg_base_q24, jcm_k5A_neg_base_q24;
static int32_t jcm_k3B_neg_base_q24, jcm_k3B_neg_depth_q24;
static int32_t jcm_k5B_neg_base_q24, jcm_k5B_neg_depth_q24;
static int32_t jcm_bright_mix_prevol_q24;
static int32_t jcm_presence_delta_q24;

/* =============================== Core process ============================ */
static inline __attribute__((always_inline)) int32_t __not_in_flash_func(process_marshall_channel)(
    int32_t s,
    int32_t* pre_hpf_state,
    int32_t* cpl1_state, int32_t* bright_state,
    int32_t* cpl2_state,
    int32_t* bass_state, int32_t* mid_lp_state, int32_t* mid_hp_state, int32_t* treble_state,
    int32_t* presence_state,
    int32_t* post_lpf_state,
    int32_t* envB_state, uint8_t* envB_decim
){
    s = qmul(s, jcm_input_pad_q24);
    s = apply_1pole_hpf(s, pre_hpf_state, jcm_pre_hpf_a_q24);
    s = apply_1pole_hpf(s, cpl1_state, jcm_cpl1_a_q24);

    if (jcm_bright_mix_q24){
        int32_t l = apply_1pole_lpf(s, bright_state, jcm_bright_a_q24);
        int32_t h = s - l;
        int32_t base       = qmul(s, jcm_prevol_q24);
        int32_t bright_add = qmul(h, jcm_bright_mix_prevol_q24);
        s = base + bright_add;
    } else {
        s = qmul(s, jcm_prevol_q24);
    }

    s = qmul(s, jcm_stageA_gain_q24);
    s = triode_ws_35_asym_fast_q24(s,
            jcm_stageA_k3_q24, jcm_stageA_k5_q24,
            jcm_k3A_neg_base_q24, jcm_k5A_neg_base_q24,
            jcm_ws_x5_on_q24,
            JCM_USE_X5);

    s = apply_1pole_hpf(s, cpl2_state, jcm_cpl2_a_q24);

    int32_t envB;
    if ( ((*envB_decim)++ & (JCM_ENV_DECIM-1)) == 0 ){
        int32_t s_abs = (s >= 0) ? s : -s;
        envB = apply_1pole_lpf(s_abs, envB_state, jcm_envB_a_q24);
    } else {
        envB = *envB_state;
    }

    int32_t k3B_neg = jcm_k3B_neg_base_q24 + qmul(jcm_k3B_neg_depth_q24, envB);
    int32_t k5B_neg = jcm_k5B_neg_base_q24 + qmul(jcm_k5B_neg_depth_q24, envB);

    s = qmul(s, jcm_stageB_gain_q24);
    s = triode_ws_35_asym_fast_q24(s,
            jcm_stageB_k3_q24, jcm_stageB_k5_q24,
            k3B_neg,           k5B_neg,
            jcm_ws_x5_on_q24,
            JCM_USE_X5);

    s = cathode_squish_q24(s, jcm_cf_amount_q24, jcm_cf_recover_q24);

    int32_t low      = apply_1pole_lpf(s, bass_state,   jcm_bass_a_q24);
    int32_t low_out  = qmul(low, jcm_bass_gain_q24);

    int32_t mid_bp   = apply_1pole_lpf(apply_1pole_hpf(s, mid_hp_state, jcm_mid_a_q24),
                                       mid_lp_state, jcm_mid_a_q24);
    int32_t mid_out  = qmul(mid_bp, jcm_mid_gain_q24);

    int32_t high_cmp = s - apply_1pole_lpf(s, treble_state, jcm_treble_a_q24);
    int32_t high_out = qmul(high_cmp, jcm_treble_gain_q24);

    int32_t mix32 = (int32_t)((int64_t)low_out + (int64_t)mid_out + (int64_t)high_out);
    mix32 = qmul(mix32, jcm_stack_makeup_q24);

#if JCM_ECO_PRES
    if (jcm_presence_gain_q24 != 0x01000000){
        int32_t pres_delta = qmul(high_cmp, jcm_presence_delta_q24);
        mix32 += pres_delta;
    }
#else
    if (jcm_presence_gain_q24 != 0x01000000){
        int32_t pres_high  = mix32 - apply_1pole_lpf(mix32, presence_state, jcm_presence_a_q24);
        int32_t pres_delta = qmul(pres_high, jcm_presence_delta_q24);
        mix32 += pres_delta;
    }
#endif

#if !JCM_ECO
    if (jcm_post_lpf_a_q24) mix32 = apply_1pole_lpf(mix32, post_lpf_state, jcm_post_lpf_a_q24);
#endif

    mix32 = qmul(mix32, jcm_master_q24);
    return clamp24(mix32);
}

/* =============================== Public API ============================== */
static inline void __not_in_flash_func(process_audio_marshall_sample)(int32_t* inout_l, int32_t* inout_r, bool stereo){
    *inout_l = process_marshall_channel(*inout_l,
        &jcm_pre_hpf_state_l, &jcm_cpl1_state_l, &jcm_bright_state_l, &jcm_cpl2_state_l,
        &jcm_bass_state_l, &jcm_mid_lp_state_l, &jcm_mid_hp_state_l, &jcm_treble_state_l,
        &jcm_presence_state_l, &jcm_post_lpf_state_l,
        &jcm_envB_state_l, &jcm_envB_decim_l);

    if(!stereo){
        *inout_r = *inout_l;
    } else {
        *inout_r = process_marshall_channel(*inout_r,
            &jcm_pre_hpf_state_r, &jcm_cpl1_state_r, &jcm_bright_state_r, &jcm_cpl2_state_r,
            &jcm_bass_state_r, &jcm_mid_lp_state_r, &jcm_mid_hp_state_r, &jcm_treble_state_r,
            &jcm_presence_state_r, &jcm_post_lpf_state_r,
            &jcm_envB_state_r, &jcm_envB_decim_r);
    }
}

static inline void __not_in_flash_func(marshall_preamp_process_block)(int32_t* in_l, int32_t* in_r, size_t frames, bool stereo){
    for (size_t i=0;i<frames;i++){
        process_audio_marshall_sample(&in_l[i], &in_r[i], stereo);
    }
}

/* =============================== Param load ============================== */
static inline void load_marshall_params_from_memory(void){
    jcm_input_pad_q24  = db_to_q24(JCM_INPUT_PAD_DB);
    jcm_pre_hpf_a_q24  = alpha_from_hz(JCM_VOICE.pre_hpf_Hz);
    jcm_cpl1_a_q24     = alpha_from_hz(JCM_VOICE.cpl1_hz);
    jcm_cpl2_a_q24     = alpha_from_hz(JCM_VOICE.cpl2_hz);
    jcm_bass_a_q24     = alpha_from_hz(JCM_VOICE.bass_hz);
    jcm_mid_a_q24      = alpha_from_hz(JCM_VOICE.mid_hz);
    jcm_treble_a_q24   = alpha_from_hz(JCM_VOICE.treble_hz);
#if !JCM_ECO_PRES
    jcm_presence_a_q24 = alpha_from_hz(JCM_VOICE.presence_hz);
#else
    jcm_presence_a_q24 = 0;
#endif
#if !JCM_ECO
    jcm_post_lpf_a_q24 = alpha_from_hz(JCM_VOICE.post_lpf_Hz);
#else
    jcm_post_lpf_a_q24 = 0;
#endif

    jcm_envB_a_q24     = alpha_from_hz(JCM_ENVB_HZ);

    jcm_stageA_gain_q24 = db_to_q24(JCM_STAGEA_GAIN);
    jcm_stageB_gain_q24 = db_to_q24(JCM_STAGEB_GAIN);
    jcm_stack_makeup_q24= db_to_q24(JCM_STACK_MAKEUP_DB);

    jcm_stageA_k3_q24 = float_to_q24(JCM_K3A);
    jcm_stageA_k5_q24 = float_to_q24(JCM_K5A);
    jcm_stageB_k3_q24 = float_to_q24(JCM_K3B);
    jcm_stageB_k5_q24 = float_to_q24(JCM_K5B);

    jcm_cf_amount_q24 = float_to_q24(0.18f + 0.12f * (JCM_VOICE.stageB_asym - 1.2f));

    int32_t pot;
    pot = storedPreampPotValue[MARSHALL][0];
    float p = (float)pot / 4095.0f;
    float t = powf(p, JCM_PREVOL_TAPER);
    float prevol_db = JCM_PREVOL_MIN_DB + (0.0f - JCM_PREVOL_MIN_DB) * t;
    prevol_db += JCM_PREVOL_TOP_BOOST_DB * powf(p, 6.0f);
    jcm_prevol_q24 = db_to_q24(prevol_db);

    int32_t prevol01 = float_to_q24(powf(p, JCM_PREVOL_TAPER));
    int32_t inv01    = 0x01000000 - prevol01;
    jcm_bright_mix_q24 = qmul(inv01, db_to_q24(JCM_BRIGHT_MAX_DB) - 0x01000000);

    float bright_fc = JCM_VOICE.bright_hz_min +
                      (JCM_VOICE.bright_hz_max - JCM_VOICE.bright_hz_min                      ) * (1.0f - p);
    jcm_bright_a_q24 = alpha_from_hz(bright_fc);

    // Tone stack gains
    pot = storedPreampPotValue[MARSHALL][1];
    jcm_bass_gain_q24   = map_pot_to_q24(pot, db_to_q24(-12.0f), db_to_q24(+6.0f));
    pot = storedPreampPotValue[MARSHALL][2];
    jcm_mid_gain_q24    = map_pot_to_q24(pot, db_to_q24(-12.0f), db_to_q24(+12.0f));
    pot = storedPreampPotValue[MARSHALL][3];
    jcm_treble_gain_q24 = map_pot_to_q24(pot, db_to_q24(-12.0f), db_to_q24(+6.0f));

    // Presence: 0..+8 dB
    pot = storedPreampPotValue[MARSHALL][4];
    jcm_presence_gain_q24 = map_pot_to_q24(pot, db_to_q24(0.0f), db_to_q24(+8.0f));

    // Master: −3..+22 dB
    pot = storedPreampPotValue[MARSHALL][5];
    jcm_master_q24 = map_pot_to_q24(pot, db_to_q24(-3.0f), db_to_q24(+22.0f));

    // --- Cached constants ---
    jcm_ws_x5_on_q24   = float_to_q24(JCM_WS_X5_ON);
    jcm_cf_recover_q24 = float_to_q24(0.97f);

    jcm_k3A_neg_base_q24 = qmul(jcm_stageA_k3_q24, float_to_q24(JCM_ASYM_A_BASE));
    jcm_k5A_neg_base_q24 = qmul(jcm_stageA_k5_q24, float_to_q24(JCM_ASYM_A_BASE));

    jcm_k3B_neg_base_q24  = qmul(jcm_stageB_k3_q24, float_to_q24(JCM_ASYM_B_BASE));
    jcm_k3B_neg_depth_q24 = qmul(jcm_stageB_k3_q24, float_to_q24(JCM_ASYM_B_DEPTH));
    jcm_k5B_neg_base_q24  = qmul(jcm_stageB_k5_q24, float_to_q24(JCM_ASYM_B_BASE));
    jcm_k5B_neg_depth_q24 = qmul(jcm_stageB_k5_q24, float_to_q24(JCM_ASYM_B_DEPTH));

    jcm_bright_mix_prevol_q24 = qmul(jcm_bright_mix_q24, jcm_prevol_q24);
    jcm_presence_delta_q24    = jcm_presence_gain_q24 - 0x01000000;

    // Reset states (avoid zipper)
    jcm_pre_hpf_state_l=jcm_pre_hpf_state_r=0;
    jcm_cpl1_state_l=jcm_cpl1_state_r=0; jcm_bright_state_l=jcm_bright_state_r=0;
    jcm_cpl2_state_l=jcm_cpl2_state_r=0;

    jcm_bass_state_l=jcm_bass_state_r=0;
    jcm_mid_lp_state_l=jcm_mid_lp_state_r=0; jcm_mid_hp_state_l=jcm_mid_hp_state_r=0;
    jcm_treble_state_l=jcm_treble_state_r=0;

    jcm_presence_state_l=jcm_presence_state_r=0;
    jcm_post_lpf_state_l=jcm_post_lpf_state_r=0;

    jcm_envB_state_l=jcm_envB_state_r=0;
    jcm_envB_decim_l=jcm_envB_decim_r=0;
}

#endif // MARSHALL_PREAMP_H

