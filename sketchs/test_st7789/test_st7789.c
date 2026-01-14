/* test_st7789 */
#include <stdio.h>
#include "../boumboum/const.h"
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"

#define PIN_SCK   18   // SPI0 SCK
#define PIN_MOSI  19   // SPI0 MOSI
#define PIN_DC    16
#define PIN_RST   20
#define PIN_CS    17

#define TFT_W 240
#define TFT_H 240

// --- SPI helpers ---

static inline void tft_cmd(uint8_t c) {
    gpio_put(PIN_DC, 0);
    gpio_put(PIN_CS, 0);
    spi_write_blocking(spi0, &c, 1);
    gpio_put(PIN_CS, 1);
}

static inline void tft_data(const uint8_t *d, size_t len) {
    gpio_put(PIN_DC, 1);
    gpio_put(PIN_CS, 0);
    spi_write_blocking(spi0, d, len);
    gpio_put(PIN_CS, 1);
}

static void tft_reset(void) {
    gpio_put(PIN_RST, 0);
    sleep_ms(20);
    gpio_put(PIN_RST, 1);
    sleep_ms(120);
}

// --- ST7789 init 240x240 “classique” ---

static void tft_init(void) {
    tft_reset();

    tft_cmd(0x01); // SWRESET
    sleep_ms(150);

    tft_cmd(0x11); // SLPOUT
    sleep_ms(150);

    // Color mode : 16 bits
    tft_cmd(0x3A);
    uint8_t colmod = 0x55;
    tft_data(&colmod, 1);

    // Memory access control (orientation + BGR)
    tft_cmd(0x36);
    uint8_t madctl = 0x00;  // simple orientation
    tft_data(&madctl, 1);

    // Porch setting
    tft_cmd(0xB2);
    uint8_t b2[] = {0x0C, 0x0C, 0x00, 0x33, 0x33};
    tft_data(b2, sizeof(b2));

    // Gate control
    tft_cmd(0xB7);
    uint8_t b7 = 0x35;
    tft_data(&b7, 1);

    // VCOM
    tft_cmd(0xBB);
    uint8_t bb = 0x19;
    tft_data(&bb, 1);

    // LCM control
    tft_cmd(0xC0);
    uint8_t c0 = 0x2C;
    tft_data(&c0, 1);

    // VDV VRH enable
    tft_cmd(0xC2);
    uint8_t c2 = 0x01;
    tft_data(&c2, 1);

    // VRH
    tft_cmd(0xC3);
    uint8_t c3 = 0x12;
    tft_data(&c3, 1);

    // VDV
    tft_cmd(0xC4);
    uint8_t c4 = 0x20;
    tft_data(&c4, 1);

    // Frame rate
    tft_cmd(0xC6);
    uint8_t c6 = 0x0F;
    tft_data(&c6, 1);

    // Power control
    tft_cmd(0xD0);
    uint8_t d0[] = {0xA4, 0xA1};
    tft_data(d0, sizeof(d0));

    // Positive gamma
    tft_cmd(0xE0);
    uint8_t e0[] = {
        0xD0,0x08,0x11,0x08,0x0C,0x15,0x39,0x33,
        0x50,0x36,0x13,0x14,0x29,0x2D
    };
    tft_data(e0, sizeof(e0));

    // Negative gamma
    tft_cmd(0xE1);
    uint8_t e1[] = {
        0xD0,0x08,0x10,0x08,0x06,0x06,0x39,0x44,
        0x51,0x0B,0x16,0x14,0x2F,0x31
    };
    tft_data(e1, sizeof(e1));

    tft_cmd(0x21); // INVON (selon module, tu peux tester 0x20/0x21/ni l’un ni l’autre)
    tft_cmd(0x29); // DISPON
    sleep_ms(100);
}

// --- fenêtre plein écran ---

static void tft_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    uint8_t caset[] = { x0>>8, x0&0xFF, x1>>8, x1&0xFF };
    uint8_t raset[] = { y0>>8, y0&0xFF, y1>>8, y1&0xFF };

    tft_cmd(0x2A);
    tft_data(caset, 4);

    tft_cmd(0x2B);
    tft_data(raset, 4);

    tft_cmd(0x2C);
}

// --- remplissage écran d'une couleur ---

static void tft_fill(uint16_t color) {
    tft_set_window(0, 0, TFT_W-1, TFT_H-1);

    uint8_t buf[TFT_W * 2];
    for (int i = 0; i < TFT_W; i++) {
        buf[2*i]   = color >> 8;    // MSB
        buf[2*i+1] = color & 0xFF;  // LSB
    }

    gpio_put(PIN_DC, 1);
    gpio_put(PIN_CS, 0);
    for (int y = 0; y < TFT_H; y++) {
        gpio_put(TEST_PIN,ON);
        spi_write_blocking(spi0, buf, sizeof(buf));
        gpio_put(TEST_PIN,OFF);
    }
    gpio_put(PIN_CS, 1);
}

// --- main ---

int main() {
    stdio_init_all();

    gpio_init(TEST_PIN);gpio_set_dir(TEST_PIN,GPIO_OUT); gpio_put(TEST_PIN,LOW);

    // SPI
    spi_init(spi0, 10000000); // 10 MHz
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

    spi_set_format(spi0,
                   8,
                   SPI_CPOL_0,
                   SPI_CPHA_0,
                   SPI_MSB_FIRST);

    // GPIO
    gpio_init(PIN_DC);  gpio_set_dir(PIN_DC, GPIO_OUT);
    gpio_init(PIN_RST); gpio_set_dir(PIN_RST, GPIO_OUT);
    gpio_init(PIN_CS);  gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_put(PIN_CS, 1);

    tft_init();

    while (1) {
        tft_fill(0x0000); // noir
        sleep_ms(800);

        tft_fill(0xFFFF); // blanc
        sleep_ms(800);

        tft_fill(0xF800); // rouge
        sleep_ms(800);

        tft_fill(0x07E0); // vert
        sleep_ms(800);

        tft_fill(0x001F); // bleu
        sleep_ms(800);
    }
}