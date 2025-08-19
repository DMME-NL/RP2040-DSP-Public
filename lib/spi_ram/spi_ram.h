/* spi_ram.h
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

#ifndef SPI_RAM_H
#define SPI_RAM_H

#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include <string.h>

#define SPI_PORT spi1
#define PIN_SCK  14
#define PIN_MOSI 11
#define PIN_MISO 12
#define PIN_CS   13

#define SPI_RAM_READ_CMD  0x03
#define SPI_RAM_WRITE_CMD 0x02

// Delay for N CPU cycles (each iteration takes ~1 cycle)
#define DELAY_CYCLES(n) \
    for (volatile int _i = 0; _i < (n); ++_i) { __asm volatile("nop"); }

static inline void spi_ram_select(void) {
    gpio_put(PIN_CS, 0);
}

static inline void spi_ram_deselect(void) {
    gpio_put(PIN_CS, 1);
}

static inline void spi_ram_write_burst(uint32_t addr, const uint8_t *data, uint32_t len) {
    uint8_t cmd[4] = { SPI_RAM_WRITE_CMD,
                       (addr >> 16) & 0xFF,
                       (addr >> 8) & 0xFF,
                       addr & 0xFF };
    spi_ram_select();
    spi_write_blocking(SPI_PORT, cmd, 4);
    //__asm volatile("nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop");  // ~10 ns at 250 MHz
    spi_write_blocking(SPI_PORT, data, len);
    spi_ram_deselect();
}

static inline void spi_ram_read_burst(uint32_t addr, uint8_t *data, uint32_t len) {
    uint8_t cmd[4] = { SPI_RAM_READ_CMD,
                       (addr >> 16) & 0xFF,
                       (addr >> 8) & 0xFF,
                       addr & 0xFF };
    spi_ram_select();
    spi_write_blocking(SPI_PORT, cmd, 4);
    //__asm volatile("nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop");  // ~10 ns at 250 MHz
    spi_read_blocking(SPI_PORT, 0x00, data, len);
    spi_ram_deselect();
}

static inline void spi_ram_init(uint8_t baudrate) {
    spi_init(SPI_PORT, baudrate * 1000000);
    //spi_set_format(SPI_PORT, 8, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST);

    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_init(PIN_CS);
    gpio_set_dir(PIN_CS, true);
    gpio_put(PIN_CS, 1);
}

static inline bool spi_ram_test(void) {
    uint8_t test_w[4] = {0xAA, 0x55, 0xCC, 0x33};
    uint8_t test_r[4] = {0};

    spi_ram_write_burst(0x000000, test_w, 4);
    spi_ram_read_burst(0x000000, test_r, 4);

    bool result = (memcmp(test_w, test_r, 4) == 0);
    if (result) {
        printf("SPI RAM test PASS\n");
    } else {
        printf("SPI RAM test FAIL\n");
    }
    return result;
}

#endif // SPI_RAM_H
