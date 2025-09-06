# 🎸✨ RP2040-DSP: Multi-Effect 24-bit Audio Processor

## 🧩 Summary

RP2040-DSP is a fully modular 24-bit stereo audio effects processor built on the Raspberry Pi RP2040 microcontroller. It is designed to run real-time audio effects for guitar and other instruments, including cabinet simulation for headphone or direct recording use.

The firmware architecture is modular, supporting multiple effects that can be stacked and configured flexibly.

<img width="2688" height="1044" alt="V1 0_Enclosure_Render" src="https://github.com/user-attachments/assets/a6f48c74-d24e-40b1-9adf-d9f274e2988c" />

---

## 🎛️ Features & Functionality

- **24-bit stereo signal path** at 48 kHz.
- **Up to 3 simultaneous effects**, each fully configurable and ordered in series.
- Modular effect system with easy header-based integration.
- Real-time parameter control via 6 potentiometers and rotary encoder.
- OLED display for clear feedback and editing.
- Footswitch-controlled bypass and slot toggling.
- Tap-tempo support for modulation and delay-based effects.
- VU meter and signal level visualization.
- Two expression pedal inputs:
  - One dedicated to master volume.
  - One assignable to any effect parameter.

> **Note:** Total number of simultaneous effects is limited by to CPU resources and their type.
> Some features, like MIDI and expression pedal, have not been implemented as of now.

---

## 🎚️ Available Effects

### Modulation

- **Chorus:** 1, 2, or 3 short modulated delay lines with optional stereo spread via phase offset.
- **Flanger:** Single short modulated delay with optional stereo phase offset.
- **Phaser:** Multi-stage all-pass filter network with optional stereo phase offset.
- **Tremolo:** Stereo amplitude modulation with optional stereo phase offset.

### Time-based

- **Delay:** Long stereo delay with different signal path and feedback modes, uses SPI RAM buffering. 
- **Reverb:** Hall / room type reverb based on comb and all-pass filters with modualr buffer size.

### Tone shaping

- **EQ:** Multi-band EQ similar to a guitar tone-stack. Includes range control for mid frequency.

### Dynamic

- **Compressor:** Adjustable threshold, ratio, attack, release, and makeup gain.
- **Distortion, Overdrive, Fuzz:** Various analog-inspired waveshaping algorithms with tone filtering similar to the EQ.

### Speaker & Preamp simulation

- **Cabinet Sim (Speaker Sim):** Parametric approximation of guitar cabinet. Includes multiple controls for tone shaping.
- **Preamp:** Simulates preamp coloration and dynamic shaping (Marshall, VOX, Fender).

### Work-in-Progress

- **Vibrato:** Pure pitch modulation using short-time delay interpolation.

---

## ⚡ Software Architecture and Design Choices

- **Header-based modularity:** Each effect is self-contained in its own header file (`*.h`), following a consistent structure:
  - `init_*()`
  - `load_*_parms_from_memory()`
  - `update_*_params_from_pots()`
  - `process_audio_*_sample()`
- **Pot and memory sync:** Parameters load from stored memory and dynamically update when pots change. Holding down the TAP button for 5 seconds stores all the effects and settings to flash, so they can be receovered after power-down.
- **Optimized math:** Using fixed point math and lightweight approximations, reducing CPU usage without harming audio quality.
- **Dual-core structure:**
  - **Core 0:** Dedicated to real-time audio sample processing.
  - **Core 1:** Handles UI, pot reading, OLED updates.
- **SPI RAM (APS6404L):** Used for long delay lines, minimizing local RAM usage.

> **Note:** I am an embedded electronics engineer and this is my first large software project in C.
> - *.c files frustrate me because I always forget to change them after modifying a header...
> - There will be some dependencies between variabled defined and used in different *.h header files.
> - I am using global variables in places where I might not need them / should not use them. 
> - There will be inconsistencies in coding style and methods as I have been learning a lot during the course of this project.
> - This has been some journey! I intend to clean up the code once all the features are there (I promisse...)
> - I will likely forget to clean up the code, and continue development on the newer hardware versions / platforms instead...

---

## 💻 CPU Usage (250 MHz OC, 48 kHz stereo)

| Effect        | CPU Peak Usage (%) |
|---------------|--------------------|
| Chorus        | 21%    |
| Compressor    | 11%    |
| Delay         | 49%    |
| Distortion    | 19%    |
| EQ            | 14%    |
| Flanger       | 18%    |
| Fuzz          | 24%    |
| Overdrive     | 24%    |
| Phaser        | 30%    |
| Preamp        | 52%    |
| Reverb        | 55%    |
| Speaker Sim   | 25%    |
| Tremolo       | 5%     |
| Vibrato       | WIP    |

> **Note:** Figures above are with common effects (overdrive, preamp, etc..) processed in mono.
> All effects can be processed as fully stereo by chaging the STEREO definition in the main.c file.
> Actual performace allows the Reveb and Delay to run simultaneously depending on the sample buffer size!
---

## 🖥️ User Interface Overview

- Parameter abbreviation display with full name and value popup on change.
- Encoder-controlled effect slot assignment and navigation.
- LED blink feedback for modulation or delays.
- Footswitch toggling per effect slot, with LED status indication.
- VU meter visualizing signal levels or compressor gain reduction in real time.
- Delay UI with time settings in [ms] or tap-tempo with selectable fractions.

---

## 🔧 Hardware Overview

> Version 1.0 Prototype

- **ADC:** PCM1808 stereo ADC module.
- **DAC:** PCM5102 stereo DAC module.
- **Regulated power:** LM78xx linear regulators.
- **Form factor:** Designed for Hammond 1950DD enclosure.
- **Input voltage:** 9–12 V DC.
- **Expression pedals:** 2 inputs, configurable.
- **❗NOTE:** Inputs are not attenuated and will clip with high-output pickups. The ADC has an input RC filter on-board that can be replaced by a ~100k series resistance to provide ~2x attenuation.

> **Later versions:**  PCM3060 version are have not yet been fully tested!. The concept was proven on a different PCB design without any changes to the software. ALthough, I did not test this PCB design specificaly, it should work perfectly fine.

---

## 📜 License

The full text of the license should be found in LICENSE.txt, included as part of this repository.

This library and all accompanying code, examples, information and documentation is Copyright (C) 2025 Milan Wendt

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public
License, version 3 as published by the Free Software Foundation.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied
warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program. If not,
see <https://www.gnu.org/licenses/>.

---

🎸 Enjoy building and tweaking your own ultimate guitar multi-effects processor with RP2040-DSP!

