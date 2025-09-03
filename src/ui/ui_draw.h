/* ui_draw.h
 * Author: Milan Wendt
 * Date:   2025-09-03
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

void drawUI(int changed_pot_index) {
    SSD1306_ClearScreen();

    // Handle pot interaction and timeout
    if (changed_pot_index >= 0) {
        if (currentUI != UI_POT) {
            previousUI = currentUI;
        }
        currentUI = UI_POT;
        last_changed_pot = changed_pot_index;
        last_pot_change_time = get_absolute_time();
    }

    if (currentUI == UI_POT) {
        int64_t us_since_change = absolute_time_diff_us(last_pot_change_time, get_absolute_time());
        if (us_since_change > 500000) {
            currentUI = previousUI;
        }
    }

    // UI rendering logic
    switch (currentUI) {
        case UI_HOME:
            // Wrap encoder position around home screen
            int offset = 0;
            // Check if delay is selected and offset for the additional TAP tempo controls
            if(selectedEffects[selected_slot] == DELAY_EFFECT_INDEX){
                if(tap_tempo_active_l) offset++;
                if(tap_tempo_active_r) offset++;
            }

            if (encoder_position < 0) encoder_position = 5 + offset;
            if (encoder_position > 5 + offset) encoder_position = 0;
            drawHomeScreen(encoder_position, param_selected, selected_slot);
            break;

        case UI_EFFECT_LIST:
            // Wrap encoder around effect list entries
            if (encoder_position < 0) encoder_position = NUM_EFFECTS - 1;
            if (encoder_position >= NUM_EFFECTS) encoder_position = 0;
            effectListIndex = encoder_position;
            drawEffectListScreen(effectListIndex);
            break;

        case UI_DELAY_MODE_MENU:
            // Wrap encoder
            if (encoder_position < 0) encoder_position = NUM_DELAY_MODES - 1;
            if (encoder_position >= NUM_DELAY_MODES) encoder_position = 0;

            delay_mode_menu_index = encoder_position;
            drawDelayModeMenu(delay_mode_menu_index);
            break;

        case UI_DELAY_FRACTION_L_MENU:
            // Wrap encoder in range
            if (encoder_position < 0) encoder_position = NUM_FRACTIONS - 1;
            if (encoder_position >= NUM_FRACTIONS) encoder_position = 0;
            // Draw the menu
            drawDelayFractionMenuL(encoder_position);
            break;

        case UI_DELAY_FRACTION_R_MENU:
            if (encoder_position < 0) encoder_position = NUM_FRACTIONS - 1;
            if (encoder_position >= NUM_FRACTIONS) encoder_position = 0;
            drawDelayFractionMenuR(encoder_position);
            break;

        case UI_CHORUS_MODE_MENU:
            // Wrap encoder
            if (encoder_position < 0) encoder_position = NUM_CHORUS_MODES - 1;
            if (encoder_position >= NUM_CHORUS_MODES) encoder_position = 0;

            chorus_mode_menu_index = encoder_position;
            drawChorusModeMenu(chorus_mode_menu_index);
            break;

        case UI_PREAMP_SELECTION: // [NEW]
            // Wrap encoder
            if (encoder_position < 0) encoder_position = NUM_PREAMPS - 1;
            if (encoder_position >= NUM_PREAMPS) encoder_position = 0;

            preamp_select_menu_index = encoder_position;
            drawPreampSelectMenu(preamp_select_menu_index);
            break;

        case UI_STEREO_MODE_MENU:
            // Wrap encoder
            if (encoder_position < 0) encoder_position = NUM_STEREO_MODES - 1;
            if (encoder_position >= NUM_STEREO_MODES) encoder_position = 0;

            stereo_mode_menu_index = encoder_position;
            drawStereoModeMenu(stereo_mode_menu_index);
            break;

        case UI_POT:
            drawPotScreen(last_changed_pot, encoder_position);
            break;

        case UI_VU_IN:
            // Wrap encoder position on VU screen
            if (encoder_position < 0) encoder_position = 1;
            if (encoder_position > 1) encoder_position = 0;

            if (absolute_time_diff_us(last_sample_time, get_absolute_time()) > 25000) {
                last_sample_time = get_absolute_time();
                peak_left_block = peak_left;
                peak_right_block = peak_right;
                peak_left = 0;
                peak_right = 0;
            }

            drawVUMeterScreen(peak_left_block, peak_right_block, encoder_position, VU_INPUT);
            break;

        case UI_VU_OUT:
            // Wrap encoder position on VU screen
            if (encoder_position < 0) encoder_position = 1;
            if (encoder_position > 1) encoder_position = 0;

            if (absolute_time_diff_us(last_sample_time, get_absolute_time()) > 25000) {
                last_sample_time = get_absolute_time();
                peak_left_block = peak_left;
                peak_right_block = peak_right;
                peak_left = 0;
                peak_right = 0;
            }

            drawVUMeterScreen(peak_left_block, peak_right_block, encoder_position, VU_OUTPUT);
            break;

        case UI_VU_GAIN:
            // Wrap encoder position on VU screen
            if (encoder_position < 0) encoder_position = 1;
            if (encoder_position > 1) encoder_position = 0;

            if (absolute_time_diff_us(last_sample_time, get_absolute_time()) > 25000) {
                last_sample_time = get_absolute_time();

                // Convert linear gain to float (0.0 to 1.0)
                float gain_l = q24_to_float(comp_linear_gain_q24_l);
                float gain_r = q24_to_float(comp_linear_gain_q24_r);

                // Convert to dB scale
                float gain_db_l = 20.0f * log10f(gain_l);
                float gain_db_r = 20.0f * log10f(gain_r);

                // Clamp to expected dB range (e.g. -40 dB to 0 dB)
                if (gain_db_l < -40.0f) gain_db_l = -40.0f;
                if (gain_db_r < -40.0f) gain_db_r = -40.0f;
                if (gain_db_l > 0.0f) gain_db_l = 0.0f;
                if (gain_db_r > 0.0f) gain_db_r = 0.0f;

                // Map -40 dB → 0, 0 dB → max needle value
                const float VU_SCALE = 2147483392.0f;  // Match drawVUMeter input range
                peak_left_block  = (uint32_t)((gain_db_l + 40.0f) * (VU_SCALE / 40.0f));
                peak_right_block = (uint32_t)((gain_db_r + 40.0f) * (VU_SCALE / 40.0f));
            }

            drawVUMeterScreen(peak_left_block, peak_right_block, encoder_position, VU_GAIN);
            break;

    }

    SSD1306_UpdateScreen();
}