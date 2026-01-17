/* test_st7789 */
#include <stdio.h>
#include <string.h>
#include "../boumboum/const.h"
#include "st7789_fonts.h"

#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "hardware/dma.h"

#define PIN_SCK   18
#define PIN_MOSI  19
#define PIN_DC    16
#define PIN_RST   20
#define PIN_CS    17
#define PIN_BL    21

#define TFT_W 240
#define TFT_H 240

static int dma_chan;
static dma_channel_config dma_cfg;
static volatile bool dma_done = false;

static uint8_t tft_frame[TFT_W * TFT_H * 2];

void dma_wait(){
    while (!dma_done) {
        tight_loop_contents();
    }
}

// ---------------------------------------------------------
// DMA IRQ : remonte CS quand le transfert complet est fini
// ---------------------------------------------------------
void dma_irq_handler() {
    dma_hw->ints0 = 1u << dma_chan;   // clear IRQ

    // attendre que le SPI ait vidé son FIFO
    while (spi_is_busy(spi0)) {
        tight_loop_contents();
    }

    gpio_put(PIN_CS, 1);              // FIN du transfert complet
    dma_done = true;
}

// ---------------------------------------------------------
// DMA init
// ---------------------------------------------------------
void init_dma_spi() {
    dma_chan = dma_claim_unused_channel(true);
    dma_cfg = dma_channel_get_default_config(dma_chan);

    channel_config_set_transfer_data_size(&dma_cfg, DMA_SIZE_8);
    channel_config_set_read_increment(&dma_cfg, true);
    channel_config_set_write_increment(&dma_cfg, false);
    channel_config_set_dreq(&dma_cfg, DREQ_SPI0_TX);

    dma_channel_set_irq0_enabled(dma_chan, true);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_irq_handler);
    irq_set_enabled(DMA_IRQ_0, true);
}

// ---------------------------------------------------------
// SPI helpers
// ---------------------------------------------------------
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

// ---------------------------------------------------------
// TON INIT GOLDEN — inchangé, byte pour byte
// ---------------------------------------------------------
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
    uint8_t madctl = 0x78;  // simple orientation
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

    tft_cmd(0x21); // INVON
    tft_cmd(0x29); // DISPON
    sleep_ms(100);
}

// ---------------------------------------------------------
// WINDOW
// ---------------------------------------------------------
static void tft_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    uint8_t caset[] = { x0>>8, x0&0xFF, x1>>8, x1&0xFF };
    uint8_t raset[] = { y0>>8, y0&0xFF, y1>>8, y1&0xFF };

    tft_cmd(0x2A);
    tft_data(caset, 4);

    tft_cmd(0x2B);
    tft_data(raset, 4);

    tft_cmd(0x2C);
}

// ---------------------------------------------------------
// FILL : 1 DMA = tout l'écran
// ---------------------------------------------------------
void tft_fill(uint16_t color) {

    dma_wait();

    static uint8_t frame[TFT_W * TFT_H * 2];

    // remplir le buffer complet
    for (int i = 0; i < TFT_W * TFT_H; i++) {
        frame[2*i]     = color >> 8;
        frame[2*i + 1] = color & 0xFF;
    }

    tft_set_window(0, 0, TFT_W - 1, TFT_H - 1);

    dma_done = false;

    gpio_put(PIN_DC, 1);
    gpio_put(PIN_CS, 0);

    gpio_put(TEST_PIN,ON);

    dma_channel_configure(
        dma_chan,
        &dma_cfg,
        &spi0_hw->dr,        // destination = SPI TX FIFO
        frame,               // source = buffer complet
        sizeof(frame),       // 115200 octets
        true                 // start
    );

    gpio_put(TEST_PIN,OFF);
}

// ---------------------------------------------------------
// FILL : 1 DMA = un rectangle dans l'écran
// ---------------------------------------------------------
void tft_fill_rect(uint16_t line_debut,
                   uint16_t colonne_debut,
                   uint16_t nbre_lignes,
                   uint16_t nbre_colonnes,
                   uint16_t color)
{

    dma_wait();

    // 1) buffer EXACT de la taille du pavé
    size_t total_pixels = nbre_lignes * nbre_colonnes;
    size_t total_bytes  = total_pixels * 2;

    // 2) remplir le buffer
    for (int i = 0; i < total_pixels; i++) {
        tft_frame[2*i]     = color >> 8;
        tft_frame[2*i + 1] = color & 0xFF;
    }

    // 3) définir la fenêtre EXACTE
    tft_set_window(
        colonne_debut,
        line_debut,
        colonne_debut + nbre_colonnes - 1,
        line_debut + nbre_lignes - 1
    );

    // 4) lancer un seul DMA pour tout le pavé
    dma_done = false;

    gpio_put(PIN_DC, 1);
    gpio_put(PIN_CS, 0);

    dma_channel_configure(
        dma_chan,
        &dma_cfg,
        &spi0_hw->dr,
        tft_frame,
        total_bytes,
        true
    );
  

}

// ---------------------------------------------------------
// CHARACTER 12x12
// ---------------------------------------------------------
void tft_draw_char_12x12(uint16_t y, uint16_t x,
                         char c,
                         uint16_t color_fg,
                         uint16_t color_bg)
{
    
    dma_wait();
    
    const uint16_t *glyph = font12x12[(uint8_t)c];

    int w = 12;
    int h = 12;

    int idx = 0;

    for (int row = 0; row < h; row++) {
        uint16_t bits = glyph[row];

        for (int col = 0; col < w; col++) {
            uint16_t color =
                (bits & (1 << (w - 1 - col))) ? color_fg : color_bg;
                //(bits & (1 << col)) ? color_fg : color_bg;

            tft_frame[idx++] = color >> 8;
            tft_frame[idx++] = color & 0xFF;
        }
    }

    // fenêtre exacte
    tft_set_window(x, y, x + w - 1, y + h - 1);

    dma_done = false;
    gpio_put(PIN_DC, 1);
    gpio_put(PIN_CS, 0);

    dma_channel_configure(
        dma_chan,
        &dma_cfg,
        &spi0_hw->dr,
        tft_frame,
        w * h * 2,   // 12×12×2 = 288 octets
        true
    );
}

void tft_draw_text_12x12(uint16_t y, uint16_t x,
                         const char *s,
                         uint16_t color_fg,
                         uint16_t color_bg)
{
    dma_wait();
    
    while (*s) {
        tft_draw_char_12x12(y, x, *s, color_fg, color_bg);
        x += 12;   // avance de 12 pixels
        s++;
    }
}

void tft_draw_text_12x12_block(
    uint16_t x,
    uint16_t y,
    const char *s,
    uint16_t fg,
    uint16_t bg)
{
    dma_wait();

    int len = 0;
    while (s[len]) len++;

    int w = len * 12;
    int h = 12;

    int idx = 0;

    // 🔥 ORDRE EXACTEMENT COMME TU LE VEUX :
    // for ligne
    //   for car
    //     for bit
    for (int ligne = 0; ligne < 12; ligne++) {

        for (int car = 0; car < len; car++) {

            const uint16_t *glyph = font12x12[(uint8_t)s[car]];
            uint16_t bits = glyph[ligne];

            for (int bit = 0; bit < 12; bit++) {

                uint16_t color =
                    (bits & (1 << (11 - bit))) ? fg : bg;

                tft_frame[idx++] = color >> 8;
                tft_frame[idx++] = color & 0xFF;
            }
        }
    }

  

    // fenêtre = position réelle sur l'écran
    tft_set_window(x, y, x + w - 1, y + h - 1);

    dma_done = false;
    gpio_put(PIN_DC, 1);
    gpio_put(PIN_CS, 0);

    dma_channel_configure(
        dma_chan,
        &dma_cfg,
        &spi0_hw->dr,
        tft_frame,
        w * h * 2,
        true
    );
}

void tft_draw_text_12x12_block_(
    uint16_t x,
    uint16_t y,
    const char *s,
    uint16_t fg,
    uint16_t bg,
    int8_t mult)
{

       dma_wait();

    if(mult<1){mult=1;}

    int len = 0;
    while (s[len]) len++;

    int w = len * 12;
    int h = 12;

    int idx = 0;

    for (int ligne = 0; ligne < 12; ligne++) {

        for (int car = 0; car < len; car++) {

            const uint16_t *glyph = font12x12[(uint8_t)s[car]];
            uint16_t bits = glyph[ligne];

            for (int bit = 0; bit < 12; bit++) {

                uint16_t color =
                    (bits & (1 << (11 - bit))) ? fg : bg;

                tft_frame[idx++] = color >> 8;
                tft_frame[idx++] = color & 0xFF;
                for(int8_t i=0;i<mult-1;i++){
                    tft_frame[idx] = tft_frame[idx-2];
                    tft_frame[idx+1] = tft_frame[idx-1];
                    idx+=2;                    
                }
            }
        }
        uint16_t curr=idx;
        for(int8_t i=0;i<(mult-1);i++){
            memcpy(&tft_frame[curr+(len*12*2*mult)*i], &tft_frame[curr - len * 12 * 2 * mult], len * 12 * 2 * mult);
            idx+=len*12*2*mult;
        }
    }

    // fenêtre = position réelle sur l'écran
    tft_set_window(x, y, x + w*mult - 1, y + h*mult - 1);

    dma_done = false;
    gpio_put(PIN_DC, 1);
    gpio_put(PIN_CS, 0);

    dma_channel_configure(
        dma_chan,
        &dma_cfg,
        &spi0_hw->dr,
        tft_frame,
        w * h * 2 * mult * mult, 
        true
    );
}

void st7789_setup(uint32_t spiSpeed)
{
    gpio_init(TEST_PIN);gpio_set_dir(TEST_PIN,GPIO_OUT); gpio_put(TEST_PIN,LOW);

    spi_init(spi0, spiSpeed);
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

    spi_set_format(spi0, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    gpio_init(PIN_DC);  gpio_set_dir(PIN_DC, GPIO_OUT);
    gpio_init(PIN_RST); gpio_set_dir(PIN_RST, GPIO_OUT);
    gpio_init(PIN_CS);  gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_put(PIN_CS, 1);
    gpio_init(PIN_BL);  gpio_set_dir(PIN_BL, GPIO_OUT);
    gpio_put(PIN_BL, 1);

    tft_init();
    init_dma_spi();
    dma_done = true;
}

// ---------------------------------------------------------
// MAIN
// ---------------------------------------------------------
int main() {
    stdio_init_all();

    st7789_setup(40000000);

    /*gpio_init(TEST_PIN);gpio_set_dir(TEST_PIN,GPIO_OUT); gpio_put(TEST_PIN,LOW);

    spi_init(spi0, 40000000);
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

    spi_set_format(spi0, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    gpio_init(PIN_DC);  gpio_set_dir(PIN_DC, GPIO_OUT);
    gpio_init(PIN_RST); gpio_set_dir(PIN_RST, GPIO_OUT);
    gpio_init(PIN_CS);  gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_put(PIN_CS, 1);
    gpio_init(PIN_BL);  gpio_set_dir(PIN_BL, GPIO_OUT);
    gpio_put(PIN_BL, 1);

    tft_init();
    init_dma_spi();
    dma_done = true;
*/

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
}