/* actions.h
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

void handleButtonPress() {

    if(DEBUG && PRINT_ACTION){ printf("Button pressed, current UI: %d, encoder position: %d\n", currentUI, encoder_position);}

    if (currentUI == UI_HOME) {
        // Build the dynamic sequence so encoder_position maps to the right item
        buildHomeItems(selected_slot);
        int idx = clampHomeIndex(encoder_position);

        switch (home_items[idx].type) {

            case HI_EFFECT_NAME:
                // Enter effect list UI; preselect current effect name
                currentUI = UI_EFFECT_LIST;
                encoder_position = selectedEffects[selected_slot];
                break;

            case HI_LEFT_TAP:
                previousUI = currentUI;
                currentUI  = UI_DELAY_FRACTION_L_MENU;
                encoder_position = (int)delay_time_fraction_l;  // preselect current L fraction
                break;

            case HI_RIGHT_TAP:
                previousUI = currentUI;
                currentUI  = UI_DELAY_FRACTION_R_MENU;
                encoder_position = (int)delay_time_fraction_r;  // preselect current R fraction
                break;

            case HI_SLOT_1: {
                int new_selected_slot = 0;
                if (selected_slot != new_selected_slot) {
                    selected_slot = new_selected_slot;
                    param_selected = true;
                }
            } break;

            case HI_SLOT_2: {
                int new_selected_slot = 1;
                if (selected_slot != new_selected_slot) {
                    selected_slot = new_selected_slot;
                    param_selected = true;
                }
            } break;

            case HI_SLOT_3: {
                int new_selected_slot = 2;
                if (selected_slot != new_selected_slot) {
                    selected_slot = new_selected_slot;
                    param_selected = true;
                }
            } break;

            case HI_RIGHT_ARROW:
                // Jump to INPUT VU; keep your original pointer convention
                currentUI = UI_VU_IN;
                encoder_position = 1; // point to right arrow
                break;

            case HI_LEFT_ARROW:
                // Jump to OUTPUT VU
                currentUI = UI_VU_OUT;
                break;

            default:
                break;
        }
    }
     
    else if (currentUI == UI_VU_IN) {
        // If encoder position is 0, go back to home
        if (encoder_position == 0) { 
            currentUI = UI_HOME;  
        } 
        else if (encoder_position == 1) { 
            // If compressor is selected, show gain reduction
            if(selectedEffects[selected_slot] == COMP_EFFECT_INDEX) {
                currentUI = UI_VU_GAIN; 
            }
            // Otherwise, go to output VU
            else{ currentUI = UI_VU_OUT; }
        }
    } 

    else if (currentUI == UI_VU_OUT) {
        if (encoder_position == 0) { 
            // If compressor is selected, show gain reduction
            if(selectedEffects[selected_slot] == COMP_EFFECT_INDEX) {
                currentUI = UI_VU_GAIN; 
            }
            // Otherwise, go back to input VU
            else{ currentUI = UI_VU_IN; }
        } 
        // Otherwise, go back to home
        else if (encoder_position == 1) { 
            currentUI = UI_HOME;
            encoder_position = 5;  // Set pointer to right arrow
        }
    }

    else if (currentUI == UI_VU_GAIN) {
        if (encoder_position == 0) { currentUI = UI_VU_IN;  } 
        else{                        currentUI = UI_VU_OUT; }
    }

    else if (currentUI == UI_EFFECT_LIST) {
        // Apply selected effect to the correct slot and return to home
        // Check if the effect is already selected in another slot
        bool effectAlreadySelected = false;
        for (int i = 0; i < 3; ++i) {
            if (i != selected_slot && selectedEffects[i] == effectListIndex) {
                // Effect already selected in another slot, show error
                // Print error message to serial console
                if(DEBUG) printf("Effect already selected in slot %d\n", i + 1);
                effectAlreadySelected = true;
                return;
            }
        }
        if (!effectAlreadySelected) {
            selectedEffects[selected_slot] = effectListIndex;
            param_selected = true;

            // Show menu for delay mode if delay effect is selected
            if (effectListIndex == DELAY_EFFECT_INDEX) {
                delay_mode_menu_index = selected_delay_mode;
                encoder_position = delay_mode_menu_index;
                currentUI = UI_DELAY_MODE_MENU;
            } 
            // Show menu for chorus effect
            else if (effectListIndex == CHRS_EFFECT_INDEX){
                chorus_mode_menu_index = selected_chorus_mode;
                encoder_position = chorus_mode_menu_index;
                currentUI = UI_CHORUS_MODE_MENU;
            } 
            // Show menu for stereo / mono mode for flanger effects
            else if (effectListIndex == FLNG_EFFECT_INDEX){
                stereo_mode_menu_index = selected_flanger_mode;
                encoder_position = stereo_mode_menu_index;
                currentUI = UI_STEREO_MODE_MENU;
            } 
            // Show menu for stereo / mono mode for phaser effects
            else if (effectListIndex == PHSR_EFFECT_INDEX){
                stereo_mode_menu_index = selected_phaser_mode;
                encoder_position = stereo_mode_menu_index;
                currentUI = UI_STEREO_MODE_MENU;
            } 
            else if (effectListIndex == TREM_EFFECT_INDEX){
                stereo_mode_menu_index = selected_tremolo_mode;
                encoder_position = stereo_mode_menu_index;
                currentUI = UI_STEREO_MODE_MENU;
            } 
            else if (effectListIndex == VIBR_EFFECT_INDEX){
                stereo_mode_menu_index = selected_vibrato_mode;
                encoder_position = stereo_mode_menu_index;
                currentUI = UI_STEREO_MODE_MENU;
            } 
            // Show menu for preamp selection [NEW]
            else if (effectListIndex == PREAMP_EFFECT_INDEX){
                preamp_select_menu_index = selected_preamp_style;
                encoder_position = preamp_select_menu_index;
                currentUI = UI_PREAMP_SELECTION;
            } 
            // Any other effect just returns to home
            else {
                encoder_position = 1;
                currentUI = UI_HOME;
            }
        }
    }
    // Set the selected delay / chorus / stereo mode and return to home
    else if (currentUI == UI_DELAY_MODE_MENU  ||
             currentUI == UI_CHORUS_MODE_MENU ||
             currentUI == UI_STEREO_MODE_MENU ||
             currentUI == UI_PREAMP_SELECTION) {
        // Changin the mode hapens in the draw function
        // That way we can update the selected mode in real time
        encoder_position = 1;  // reset to effect name
        currentUI = UI_HOME;
    } 

    else if (currentUI == UI_DELAY_FRACTION_L_MENU) {
        // Commit selected item and return to HOME
        delay_time_fraction_l = (DelayFraction)encoder_position;
        currentUI = UI_HOME;

        // Optional: place cursor back on the L tap if still visible
        buildHomeItems(selected_slot);
        for (int i = 0; i < home_items_count; ++i) {
            if (home_items[i].type == HI_LEFT_TAP) { encoder_position = i; break; }
        }
    }
    else if (currentUI == UI_DELAY_FRACTION_R_MENU) {
        delay_time_fraction_r = (DelayFraction)encoder_position;
        currentUI = UI_HOME;

        buildHomeItems(selected_slot);
        for (int i = 0; i < home_items_count; ++i) {
            if (home_items[i].type == HI_RIGHT_TAP) { encoder_position = i; break; }
        }
    }

    // Undefined UI and action
    else {
        if(DEBUG){ 
            printf("[!] Undefined UI for button press: %d\n", currentUI); 
            printf("    Returning back to HOME\n"); 
        }
        encoder_position = 1;  // reset to effect name
        currentUI = UI_HOME;
    } 
}