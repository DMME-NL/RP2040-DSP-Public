/* ssd1306.c
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

#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <stdint.h>
#include <stdbool.h>

#include "ssd1306.h"
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "font.h"

#define I2C_PORT i2c0
#define SSD1306_ADDR 0x3C

uint8_t screen_buffer[SSD1306_BUFFER_SIZE];
uint8_t old_screen_buffer[SSD1306_BUFFER_SIZE];

#define SSD1306_COMMAND 0x00
#define SSD1306_DATA    0x40

static void SSD1306_SendCommand(uint8_t command) {
    uint8_t data[2] = {SSD1306_COMMAND, command};
    i2c_write_blocking(I2C_PORT, SSD1306_ADDR, data, 2, false);
}

static void SSD1306_SendData(uint8_t data) {
    uint8_t buffer[2] = {SSD1306_DATA, data};
    i2c_write_blocking(I2C_PORT, SSD1306_ADDR, buffer, 2, false);
}

void SSD1306_Init(void) {
    SSD1306_SendCommand(0xAE);  // Display off
    SSD1306_SendCommand(0xD5);  // Set display clock divide ratio/oscillator frequency
    SSD1306_SendCommand(0x80);  // Set divide ratio
    SSD1306_SendCommand(0xA8);  // Set multiplex ratio
    SSD1306_SendCommand(0x3F);  // 1/64 duty
    SSD1306_SendCommand(0xD3);  // Set display offset
    SSD1306_SendCommand(0x00);  // No offset
    SSD1306_SendCommand(0x40);  // Set start line
    SSD1306_SendCommand(0xA1);  // Set segment re-map
    SSD1306_SendCommand(0xC8);  // Set COM output scan direction
    SSD1306_SendCommand(0xDA);  // Set COM pins hardware configuration 
    SSD1306_SendCommand(0x12);  
    SSD1306_SendCommand(0x81);  // Set contrast control
    SSD1306_SendCommand(0x7F);  
    SSD1306_SendCommand(0xA4);  // Disable entire display on
    SSD1306_SendCommand(0xA6);  // Set normal display
    SSD1306_SendCommand(0xD9);  // Set pre-charge period
    SSD1306_SendCommand(0xF1);  
    SSD1306_SendCommand(0xDB);  // Set VCOMH deselect level
    SSD1306_SendCommand(0x40);  
    SSD1306_SendCommand(0x8D);  // Enable charge pump
    SSD1306_SendCommand(0x14);  
    SSD1306_SendCommand(0xAF);  // Display on
}

void SSD1306_ClearScreen(void) {
    memset(screen_buffer, 0x00, sizeof(screen_buffer));
}

void SSD1306_UpdateScreen() {
    // The SSD1306 display is organized in 8 pages (rows), each 8 pixels tall
    for (uint8_t page = 0; page < 8; page++) {
        const uint16_t offset = page * SCREEN_WIDTH;

        // Compare this page in the new buffer with the old buffer
        if (memcmp(&screen_buffer[offset], &old_screen_buffer[offset], SCREEN_WIDTH) != 0) {
            // Page has changed — prepare to update it

            // Set page address
            SSD1306_SendCommand(0xB0 + page);

            // Set column address to 0
            SSD1306_SendCommand(0x00); // Lower nibble
            SSD1306_SendCommand(0x10); // Upper nibble

            // Send changed data to the screen
            for (uint8_t col = 0; col < SCREEN_WIDTH; col++) {
                uint8_t data = screen_buffer[offset + col];
                SSD1306_SendData(data);

                // Update old buffer to match
                old_screen_buffer[offset + col] = data;
            }
        }
    }
}

void SSD1306_DrawPixel(int x, int y, bool color) {
    if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) return;

    int byteIndex = x + (y / 8) * SCREEN_WIDTH;
    uint8_t bitMask = 1 << (y % 8);

    if (color)
        screen_buffer[byteIndex] |= bitMask;
    else
        screen_buffer[byteIndex] &= ~bitMask;
}

void SSD1306_DrawLine(int x0, int y0, int x1, int y1, bool color) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;

    while (1) {
        SSD1306_DrawPixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void SSD1306_DrawChar(int x, int y, char c, bool inverted) {
    if (!ActiveFont || c < ActiveFont->first_char || c > ActiveFont->last_char)
        return;

    int font_index = c - ActiveFont->first_char;
    int bytes_per_column = (ActiveFont->height + 7) / 8; // Number of bytes per column
    int char_data_size = ActiveFont->width * bytes_per_column;

    for (int col = 0; col < ActiveFont->width; col++) {
        for (int row_byte = 0; row_byte < bytes_per_column; row_byte++) {
            uint8_t byte = ActiveFont->data[font_index * char_data_size + col * bytes_per_column + row_byte];
            if (inverted) byte = ~byte;

            for (int bit = 0; bit < 8; bit++) {
                int pixel_y = y + row_byte * 8 + bit;
                if (pixel_y >= y + ActiveFont->height) break; // Avoid drawing beyond character height
                if (byte & (1 << bit)) {
                    SSD1306_DrawPixel(x + col, pixel_y, true);
                } else {
                    SSD1306_DrawPixel(x + col, pixel_y, false);
                }
            }
        }
    }
}

void SSD1306_DrawString(int x, int y, const char *str, bool inverted) {
    while (*str && x < SCREEN_WIDTH - ActiveFont->width) {
        SSD1306_DrawChar(x, y, *str++, inverted);
        x += ActiveFont->width + 1;
    }
}

void SSD1306_DrawRect(int x, int y, int w, int h, bool color) {
    SSD1306_DrawLine(x, y, x + w - 1, y, color);
    SSD1306_DrawLine(x, y + h - 1, x + w - 1, y + h - 1, color);
    SSD1306_DrawLine(x, y, x, y + h - 1, color);
    SSD1306_DrawLine(x + w - 1, y, x + w - 1, y + h - 1, color);
}

void SSD1306_FillRect(int x, int y, int w, int h, bool color) {
    for (int i = 0; i < h; i++) {
        SSD1306_DrawLine(x, y + i, x + w - 1, y + i, color);
    }
}

void SSD1306_DrawCircle(int x0, int y0, int radius, bool color) {
    int x = radius;
    int y = 0;
    int err = 1 - x;

    while (x >= y) {
        SSD1306_DrawPixel(x0 + x, y0 + y, color);
        SSD1306_DrawPixel(x0 - x, y0 + y, color);
        SSD1306_DrawPixel(x0 + x, y0 - y, color);
        SSD1306_DrawPixel(x0 - x, y0 - y, color);
        SSD1306_DrawPixel(x0 + y, y0 + x, color);
        SSD1306_DrawPixel(x0 - y, y0 + x, color);
        SSD1306_DrawPixel(x0 + y, y0 - x, color);
        SSD1306_DrawPixel(x0 - y, y0 - x, color);

        y++;
        if (err <= 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

void SSD1306_FillCircle(int x0, int y0, int radius, bool color) {
    int x = radius;
    int y = 0;
    int err = 1 - x;

    while (x >= y) {
        SSD1306_DrawLine(x0 - x, y0 + y, x0 + x, y0 + y, color);
        SSD1306_DrawLine(x0 - x, y0 - y, x0 + x, y0 - y, color);
        SSD1306_DrawLine(x0 - y, y0 + x, x0 + y, y0 + x, color);
        SSD1306_DrawLine(x0 - y, y0 - x, x0 + y, y0 - x, color);

        y++;
        if (err <= 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

void SSD1306_DrawTriangle(int x0, int y0, int x1, int y1, int x2, int y2, bool color) {
    SSD1306_DrawLine(x0, y0, x1, y1, color);
    SSD1306_DrawLine(x1, y1, x2, y2, color);
    SSD1306_DrawLine(x2, y2, x0, y0, color);
}

// Helper to swap two integers
static void _swap_int(int *a, int *b) {
    int t = *a;
    *a = *b;
    *b = t;
}

// Filled triangle using scanline filling
void SSD1306_FillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, bool color) {
    // Sort by Y order
    if (y0 > y1) { _swap_int(&y0, &y1); _swap_int(&x0, &x1); }
    if (y1 > y2) { _swap_int(&y1, &y2); _swap_int(&x1, &x2); }
    if (y0 > y1) { _swap_int(&y0, &y1); _swap_int(&x0, &x1); }

    int total_height = y2 - y0;
    for (int i = 0; i < total_height; i++) {
        bool second_half = i > y1 - y0 || y1 == y0;
        int segment_height = second_half ? y2 - y1 : y1 - y0;
        float alpha = (float)i / total_height;
        float beta  = (float)(i - (second_half ? y1 - y0 : 0)) / segment_height;

        int ax = x0 + (x2 - x0) * alpha;
        int bx = second_half
               ? x1 + (x2 - x1) * beta
               : x0 + (x1 - x0) * beta;

        if (ax > bx) _swap_int(&ax, &bx);
        SSD1306_DrawLine(ax, y0 + i, bx, y0 + i, color);
    }
}

const uint8_t splash_logo_bitmap[512] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f, 0x7f, 0x3f, 0x9f, 0x8f, 
	0xcf, 0xc7, 0xe7, 0xe3, 0xf3, 0xf3, 0xf9, 0xf9, 0xf9, 0xf9, 0xfc, 0xfc, 0xfc, 0xfc, 0xfc, 0xfc, 
	0xfc, 0xfc, 0xfc, 0xfc, 0xfc, 0xfc, 0xf9, 0xf9, 0xf9, 0xf9, 0xf3, 0xf3, 0xe3, 0xe7, 0xc7, 0xcf, 
	0x9f, 0x1f, 0x3f, 0x7f, 0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
	0xff, 0xff, 0xff, 0xff, 0x7f, 0x3f, 0x1f, 0xc7, 0xe3, 0xf1, 0xf8, 0xfc, 0xfe, 0xff, 0xff, 0x7f, 
	0x1f, 0x0f, 0x07, 0x07, 0x03, 0x03, 0xc1, 0xff, 0xff, 0xff, 0xff, 0x7f, 0x3f, 0x1f, 0x0f, 0x0f, 
	0x07, 0x07, 0x83, 0xf3, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
	0xff, 0xff, 0xff, 0xfe, 0xfc, 0xf8, 0xf1, 0xe3, 0x87, 0x1f, 0x3f, 0xff, 0xff, 0xff, 0xff, 0xff, 
	0xff, 0x3f, 0x07, 0xc1, 0xf0, 0xfc, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x07, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0xbc, 0xdf, 0xef, 0xf7, 0x3b, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0xf0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfc, 0xf0, 0xc1, 0x0f, 0x3f, 0xff, 
	0x03, 0x00, 0xfc, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xe7, 0xe0, 0xe0, 0xf0, 
	0xf8, 0xf8, 0xfc, 0xfe, 0xff, 0xff, 0xff, 0x7f, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xe0, 
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f, 0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f, 0xbf, 0xff, 0xff, 0xff, 0xff, 0xfc, 0x00, 0x07, 
	0xc0, 0x00, 0x3f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xe0, 0xfe, 0xff, 
	0xff, 0xff, 0x1f, 0x07, 0x03, 0x01, 0x01, 0x00, 0x00, 0x00, 0xf0, 0xff, 0xff, 0xff, 0xff, 0xff, 
	0xff, 0x7f, 0xbf, 0xdf, 0xe7, 0xfb, 0xfd, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x3f, 0x00, 0xe0, 
	0xff, 0xfc, 0xe0, 0x83, 0x0f, 0x3f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
	0xff, 0xff, 0xff, 0xff, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xf0, 0x7e, 0xbf, 0xdf, 0xef, 
	0x77, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0x7e, 0xbf, 0xdf, 0xdf, 0xef, 0xf7, 0xfb, 
	0xfd, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x3f, 0x0f, 0x83, 0xf0, 0xfc, 0xff, 
	0xff, 0xff, 0xff, 0xff, 0xfe, 0xfc, 0xf8, 0xe3, 0xc7, 0x8f, 0x1f, 0x3f, 0x7f, 0xff, 0xff, 0xff, 
	0xff, 0xff, 0xc7, 0xc0, 0xc0, 0xe0, 0xe0, 0xf0, 0xf8, 0xf8, 0xfd, 0xfe, 0xff, 0xff, 0xff, 0xf7, 
	0xf0, 0xf0, 0xf0, 0xf8, 0xf8, 0xfc, 0xfc, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
	0xff, 0xff, 0xff, 0x7f, 0x3f, 0x1f, 0x8f, 0xc7, 0xe1, 0xf8, 0xfc, 0xff, 0xff, 0xff, 0xff, 0xff, 
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0xfe, 0xfc, 0xf9, 0xf1, 
	0xf3, 0xe3, 0xe7, 0xc7, 0xcf, 0xcf, 0x9f, 0x9f, 0x9f, 0x9f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 
	0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x9f, 0x9f, 0x9f, 0x9f, 0xcf, 0xcf, 0xc7, 0xe7, 0xe3, 0xf3, 
	0xf9, 0xf8, 0xfc, 0xfe, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

// vertical draw bitmap
void oled_draw_bitmap(int x, int y, int w, int h, const uint8_t *bitmap, bool inverted) {
    for (int bx = 0; bx < w; bx++) {
        for (int by = 0; by < h; by++) {
            int byte_index = (bx) + (by / 8) * w;
            uint8_t byte = bitmap[byte_index];

            bool pixel_on = byte & (1 << (by % 8));
            if (inverted) pixel_on = !pixel_on;

            SSD1306_DrawPixel(x + bx, y + by, pixel_on);
        }
    }
}

void SSD1306_DrawSplashLogoBitmap(int x, int y, bool inverted) {
    oled_draw_bitmap(x, y, 64, 64, splash_logo_bitmap, inverted);
    SSD1306_UpdateScreen();
}