/* test_st7789 */
#include <stdio.h>
#include <string.h>
#include "st7789.h"
#include "../boumboum/const.h"
#include "st7789_fonts.h"

#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "hardware/dma.h"



int main() {
    stdio_init_all();

    st7789_setup(40000000);


    while (1) {

        tft_fill(0x0000); // écran noir

        tft_draw_text_12x12_block(0, 16, "+HELLO World!", 0xFFFF, 0x0000);

        tft_draw_text_12x12_block(0, 32, "AaBbCcDdEeFfGgHhIiJj", 0xFFFF, 0x0000);
        tft_draw_text_12x12_block(0, 48, "KkLlMmNnOoPpQqRrSsTt", 0xFFFF, 0x0000);
        tft_draw_text_12x12_block(0, 64, "UuVvWwXxYyZz", 0xFFFF, 0x0000);
        tft_draw_text_12x12_block(0, 80, "$+-*/,;:?\%&#()[]{}", 0xFFFF, 0x0000);
        tft_draw_text_12x12_block_(0, 108, "$+-*/,;:?", 0xFFFF, 0x0000,2);
        tft_draw_text_12x12_block_(0, 134, "\%&#()[]{}", 0xFFFF, 0x0000,2);
        sleep_ms(100);  gpio_put(TEST_PIN, 1);
        tft_draw_text_12x12_block_(0, 160, "0123456789", 0xFFFF, 0x0000,2);
        gpio_put(TEST_PIN, 0);
        tft_draw_text_12x12_block_(0, 200, "012345", 0xFFFF, 0x0000,3);

        sleep_ms(5000);
    
        tft_fill_rect(0,0,TFT_H,TFT_W, 0x0000);
        tft_fill_rect(50, 50, 140, 140, 0xFFFF);
        tft_fill_rect(96,50,4,140, 0x0000);
        tft_draw_text_12x12_block_(50, 100, "ST7789", 0xFFFF, 0x0000,2);
        sleep_ms(2000);

        tft_fill_rect(0,0,TFT_H,TFT_W, 0xFFFF);
        tft_fill_rect(50, 50, 140, 140, 0x0000);
        tft_fill_rect(96,50,4,140, 0xFFFF);
        tft_draw_text_12x12_block_(50, 100, "ST7789", 0x0000, 0xFFFF,2);
        sleep_ms(2000);

        tft_fill_rect(0,0,TFT_W,TFT_H, 0x07E0); // vert
        tft_fill_rect(50, 50, 140, 140, 0x001F);
        sleep_ms(1000);

        tft_fill_rect(0,0,TFT_W,TFT_H, 0x001F); // rouge
        tft_fill_rect(50, 50, 140, 140, 0xF800);
        sleep_ms(1000);

        tft_fill_rect(0,0,TFT_W,TFT_H, 0xF800); // bleu
        tft_fill_rect(50, 50, 140, 140, 0x07E0);
        sleep_ms(1000);
    }
    return 1;
}