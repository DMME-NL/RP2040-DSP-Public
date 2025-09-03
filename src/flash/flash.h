/* flash.h
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

#pragma once
/* flash.h — Safe journaled settings storage for RP2040
   - Places settings right after the firmware image (4KB aligned)
   - Slot size default 256 B (must be multiple of 256). Bump to 512 B if your record won't fit.
   - Erase granularity 4 KB (can reserve multiple sectors via SETTINGS_SECTORS)
   - All erase/program runs from SRAM with IRQs masked; NO multicore lockout here.
   - Caller (Core 0) must cooperatively park Core 1 around the save.
*/

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "hardware/flash.h"
#include "hardware/sync.h"

// --------------------------- Config / layout --------------------------------

// End of firmware image in XIP space (provided by SDK/linker)
extern uint8_t __flash_binary_end;

// Total flash size (override via build flags/boards.cmake if needed)
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (2u * 1024u * 1024u)
#endif

#define SETTINGS_SECTOR_SIZE   4096u

// How many 4KB sectors to reserve for settings (1 or 2 are typical)
#ifndef SETTINGS_SECTORS
#define SETTINGS_SECTORS       1u
#endif

// Program page size MUST be a multiple of 256
#ifndef SETTINGS_SLOT_SIZE
#define SETTINGS_SLOT_SIZE     256u      // set to 512u if your record > 256 B
#endif

#define SETTINGS_AREA_SIZE     (SETTINGS_SECTORS * SETTINGS_SECTOR_SIZE)
#define SETTINGS_NUM_SLOTS     (SETTINGS_AREA_SIZE / SETTINGS_SLOT_SIZE)

// First 4KB-aligned sector AFTER the image
#define SETTINGS_OFFSET_MIN \
    (((uintptr_t)&__flash_binary_end - XIP_BASE + (SETTINGS_SECTOR_SIZE - 1)) & ~(SETTINGS_SECTOR_SIZE - 1))

// Highest legal start for our reserved area
#define SETTINGS_OFFSET_MAX (PICO_FLASH_SIZE_BYTES - SETTINGS_AREA_SIZE)

#if SETTINGS_OFFSET_MIN > SETTINGS_OFFSET_MAX
#error "Not enough free flash for settings area (after image). Reduce firmware size or raise flash size."
#endif

#define SETTINGS_FLASH_OFFSET SETTINGS_OFFSET_MIN

// --------------------- Project globals (declared elsewhere) ------------------

// Working/live state used by the app
// extern uint16_t storedPotValue[NUM_EFFECTS][NUM_FUNC_POTS];
// extern uint16_t storedPreampPotValue[NUM_PREAMPS][NUM_FUNC_POTS];
// extern uint8_t  selectedEffects[3];
// extern uint8_t  led_state;
// extern uint8_t  default_led_state;

// Defaults: either define them in ONE .c file, or define FLASH_DEFINE_DEFAULTS
const uint16_t defaultPotValue[NUM_EFFECTS][NUM_FUNC_POTS] = {
    {  600, 2500,    0, 3000, 3000, 2500 },   // 0  CHORUS
    { 2500,  650,    0,  200,    0, 2000 },   // 1  COMPRESSOR
    { 1000, 2000, 2000, 1000, 1200, 2000 },   // 2  DELAY
    { 2000, 3000, 1500, 2000, 2000, 2000 },   // 3  DISTORTION
    { 2000, 2000, 2000, 2000, 4000, 2000 },   // 4  EQ
    { 1000, 1000, 2500, 2000, 3000, 2000 },   // 5  FLANGER
    { 2000, 2000, 2000, 2000, 2000, 2000 },   // 6  FUZZ
    { 2000, 2000, 2000, 2000, 2000, 2000 },   // 7  OVERDRIVE
    {  500, 1250, 3000, 3000, 3000, 2500 },   // 8  PHASER
    { 2000, 2000, 2000, 2000, 2000, 2000 },   // 9  PREAMP
    { 2200, 3600, POT_MAX, 3000, POT_MAX, 2000 }, // 10 REVERB
    { 2000, 3000, 1800, 2000, 2500, 2000 },   // 11 CAB SIM
    { 2000, 2000,    0,    0,    0,    0 },   // 12 TREMOLO
    { 2000, 2000, 2000,    0,    0,    0 },   // 13 VIBRATO
};
const uint16_t defaultPreampPotValue[NUM_PREAMPS][NUM_FUNC_POTS] = {
    { 2000, 2000, 2000, 2000, 2000, 2000 },   // 0 FENDER
    { 2000, 2000, 2000, 2000, 2000, 2000 },   // 1 VOX
    { 2000, 2000, 2000, 2000, 2000, 2000 },   // 2 MARSHALL
    { 2000, 2000, 2000, 2000, 2000, 2000 },   // 3 SOLDANO
};
const uint8_t  defaultSelectedEffects[3] = {9, 2, 10};
const uint8_t  default_led_state_const   = 0x04;

// defaults (right after defaultSelectedEffects/default_led_state)
const uint8_t  default_selected_slot   = 0;        // 0..2
const uint32_t default_tap_interval_ms = 500;               // default tap tempo interval (ms)
const bool default_tap_tempo_active_l = false;
const bool default_tap_tempo_active_r = false;  

// ------------------------------ Record type ----------------------------------

typedef struct {
    uint32_t seq;                        // monotonically increasing
    uint32_t crc;                        // checksum excluding 'crc' bytes
    uint16_t pot[NUM_EFFECTS][NUM_FUNC_POTS];
    uint16_t preamp[NUM_PREAMPS][NUM_FUNC_POTS];
    uint8_t  selectedEffects[3];
    uint8_t  default_led_state;

    uint8_t  selected_slot;              // 0..2
    uint32_t tap_interval_ms;
    uint8_t  delay_time_fraction_l;
    uint8_t  delay_time_fraction_r; 
} SettingsRecord;

_Static_assert(SETTINGS_SLOT_SIZE % 256u == 0, "SETTINGS_SLOT_SIZE must be a multiple of 256");
_Static_assert(sizeof(SettingsRecord) <= SETTINGS_SLOT_SIZE,
               "SettingsRecord must fit in SETTINGS_SLOT_SIZE (raise to 512 if needed)");

// Internal working copy + last-saved shadow
static SettingsRecord g_settings;            // local to each TU including this header
static SettingsRecord last_saved_settings;   // zero-init (BSS)

// ------------------------------ CRC helper -----------------------------------

static inline uint32_t settings_crc(const SettingsRecord* rec) {
    const uint8_t* p   = (const uint8_t*)rec;
    const size_t   len = sizeof(SettingsRecord);
    const size_t   off = offsetof(SettingsRecord, crc);
    uint32_t s = 0;
    for (size_t i = 0; i < len; ++i) {
        if (i >= off && i < off + sizeof(uint32_t)) continue; // skip crc bytes
        s += p[i];
    }
    return s;
}

// -------------------------- Flash view helpers --------------------------------

static inline const uint8_t* settings_flash_base(void) {
    return (const uint8_t*)(XIP_BASE + SETTINGS_FLASH_OFFSET);
}
static inline const SettingsRecord* slot_ptr(int i) {
    return (const SettingsRecord*)(settings_flash_base() + (size_t)i * SETTINGS_SLOT_SIZE);
}

// Latest valid record by seq
static inline const SettingsRecord* find_latest_record(uint32_t* out_max_seq) {
    uint32_t max_seq = 0;
    const SettingsRecord* best = NULL;
    for (int i = 0; i < (int)SETTINGS_NUM_SLOTS; ++i) {
        const SettingsRecord* r = slot_ptr(i);
        if (r->crc == settings_crc(r)) {
            if (r->seq >= max_seq) { // >= so last wins on ties
                max_seq = r->seq;
                best = r;
            }
        }
    }
    if (out_max_seq) *out_max_seq = max_seq;
    return best;
}

// Next slot + whether we must erase (wrap)
static inline void plan_next_slot(int* out_slot_index, bool* out_need_erase) {
    uint32_t max_seq = 0;
    int last_slot = -1;
    for (int i = 0; i < (int)SETTINGS_NUM_SLOTS; ++i) {
        const SettingsRecord* r = slot_ptr(i);
        if (r->crc == settings_crc(r)) {
            if (r->seq >= max_seq) {
                max_seq = r->seq;
                last_slot = i;
            }
        }
    }
    int next = (last_slot < 0) ? 0 : ((last_slot + 1) % (int)SETTINGS_NUM_SLOTS);
    bool erase = (next == 0); // wrap → erase the whole reserved area
    *out_slot_index = next;
    *out_need_erase = erase;
}

// --------------------------- Flash commit (SRAM) ------------------------------
// Assumes Core 1 is cooperatively parked by the caller.
// We only mask IRQs, no multicore lockout here.
static __not_in_flash_func(void settings_flash_commit)(
    int slot_index,
    const uint8_t* slot_image,
    bool erase_first
){
    uint32_t irq = save_and_disable_interrupts();

    if (erase_first) {
        if(DEBUG) printf("Erasing flash for settings...\n");
        flash_range_erase(SETTINGS_FLASH_OFFSET, SETTINGS_AREA_SIZE);
    }
    if(DEBUG) printf("Writing settings to flash (slot %d)...\n", slot_index);
    flash_range_program(SETTINGS_FLASH_OFFSET + (size_t)slot_index * SETTINGS_SLOT_SIZE,
                        slot_image, SETTINGS_SLOT_SIZE);

    restore_interrupts(irq);
}

// ------------------------------- Public API -----------------------------------

static inline bool load_settings_from_flash(SettingsRecord* out) {
    uint32_t max_seq = 0;
    const SettingsRecord* best = find_latest_record(&max_seq);
    if (!best) return false;
    memcpy(out, best, sizeof(SettingsRecord));
    return (out->crc == settings_crc(out));
}

// Journaled save (prepares image, then commits)
static inline void save_settings_to_flash(const SettingsRecord* rec_in) {
    int  slot_index;
    bool need_erase;
    plan_next_slot(&slot_index, &need_erase);

    SettingsRecord tmp = *rec_in;
    uint32_t max_seq = 0; (void)find_latest_record(&max_seq);
    tmp.seq = max_seq + 1;
    tmp.crc = settings_crc(&tmp);

    // Build one slot image (pad to slot size with 0xFF)
    uint8_t slot_image[SETTINGS_SLOT_SIZE];
    memset(slot_image, 0xFF, sizeof(slot_image));
    memcpy(slot_image, &tmp, sizeof(tmp));

    // Do the critical section (erase/program) from SRAM
    settings_flash_commit(slot_index, slot_image, need_erase);
}

// helper to validate
static inline DelayFraction validate_fraction(uint8_t value) {
    if (value >= NUM_FRACTIONS) return QUARTER; // fallback default
    return (DelayFraction)value;
}

// Initialize working state from flash or defaults
static inline void init_settings_from_flash(void) {
    if (!load_settings_from_flash(&g_settings)) {
        memset(&g_settings, 0, sizeof(g_settings));
        memcpy(g_settings.pot,    defaultPotValue,       sizeof(g_settings.pot));
        memcpy(g_settings.preamp, defaultPreampPotValue, sizeof(g_settings.preamp));
        memcpy(g_settings.selectedEffects, defaultSelectedEffects, sizeof(g_settings.selectedEffects));
        g_settings.default_led_state = default_led_state_const;

        // NEW defaults
        g_settings.selected_slot            = default_selected_slot;
        g_settings.tap_interval_ms          = default_tap_interval_ms;
        g_settings.delay_time_fraction_l    = QUARTER;
        g_settings.delay_time_fraction_r    = QUARTER;
    }

    // Push to live working vars
    memcpy(storedPotValue,       g_settings.pot,    sizeof(g_settings.pot));
    memcpy(storedPreampPotValue, g_settings.preamp, sizeof(g_settings.preamp));
    memcpy(selectedEffects,      g_settings.selectedEffects, sizeof(selectedEffects));
    default_led_state = g_settings.default_led_state;

    // NEW: push to live
    selected_slot            = g_settings.selected_slot;
    tap_interval_ms          = g_settings.tap_interval_ms;
    delay_time_fraction_l    = validate_fraction(g_settings.delay_time_fraction_l);
    delay_time_fraction_r    = validate_fraction(g_settings.delay_time_fraction_r);

}


// High-level: copy live->g_settings and save only if changed
static __not_in_flash_func(void save_all_settings_to_flash)(void) {
    memcpy(g_settings.pot,    storedPotValue,       sizeof(g_settings.pot));
    memcpy(g_settings.preamp, storedPreampPotValue, sizeof(g_settings.preamp));
    memcpy(g_settings.selectedEffects, selectedEffects, sizeof(g_settings.selectedEffects));
    g_settings.default_led_state = led_state;

    // NEW: capture selected slot and delay times
    g_settings.selected_slot           = selected_slot;
    g_settings.tap_interval_ms         = tap_interval_ms;
    g_settings.delay_time_fraction_l   =  (uint8_t)delay_time_fraction_l;
    g_settings.delay_time_fraction_r   =  (uint8_t)delay_time_fraction_r;

    if (memcmp(&g_settings, &last_saved_settings, sizeof(SettingsRecord)) == 0) return;

    if (DEBUG) printf("Saving to flash.\n");
    save_settings_to_flash(&g_settings);
    last_saved_settings = g_settings;
}