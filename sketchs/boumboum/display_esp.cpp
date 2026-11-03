#include "const.h"

#ifndef ST7789

// ---------------------------------------------------------
// display through the ESP32 board (SPI link, see esp_lcd_link)
// a packet = one or more orders: letter, n (2 bytes, low first), n data bytes
// ---------------------------------------------------------
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "hardware/dma.h"
#include "hardware/sync.h"
#include "hardware/irq.h"
#include "display_esp.h"
#include "frequences.h"
#include "util.h"

extern int                disp_dma_chan;    // screen.cpp
extern dma_channel_config disp_dma_cfg;     // screen.cpp
extern spin_lock_t*       disp_dma_lock;    // screen.cpp

static volatile bool     esp_ready = false;     // rising edge seen on DISP_PIN_READY since the last packet
static volatile bool     dma_busy = false;      // a packet is being sent
static volatile bool     link_down = false;     // the ESP did not answer: drop instead of waiting
static volatile bool     pend_full = false;     // a packet is waiting for the ESP
static const uint8_t* volatile pend_buf;
static volatile uint16_t pend_len;
static const uint8_t* volatile cur_buf = nullptr; // packet being sent
static volatile uint8_t  cur_pad;               // zeros to add after it (multiple of 4)
static volatile uint32_t nb_dropped = 0;

static uint8_t small_buf[LNK_NB_SMALL_BUF][LNK_SMALL_BUF_SIZE] __attribute__((aligned(4)));
static uint8_t small_idx = 0;

static bool     state_known = false;            // last colours/multiplier sent to the ESP
static uint16_t last_fg, last_bg;
static uint8_t  last_mult;

// ---------------------------------------------------------
// low level: must be called with disp_dma_lock held
// ---------------------------------------------------------
static void __not_in_flash_func(start_packet)(const uint8_t* buf, uint16_t len)
{
    esp_ready = false;                  // the ESP lowers READY when CS rises
    dma_busy = true;
    cur_buf = buf;
    cur_pad = LNK_PAD4(len) - len;
    gpio_put(DISP_PIN_CS, 0);
    dma_channel_configure(disp_dma_chan, &disp_dma_cfg, &spi0_hw->dr, buf, len, true);
}

// end of DMA: the SPI is idle (checked by disp_dma_irq_handler)
void __not_in_flash_func(esp_dma_end)()
{
    uint32_t f = spin_lock_blocking(disp_dma_lock);
    for (uint8_t i = 0; i < cur_pad; i++) {     // at most 3 bytes: the FIFO holds 8
        spi0_hw->dr = 0;
    }
    while (spi_is_busy(spi0)) {
        tight_loop_contents();
    }
    gpio_put(DISP_PIN_CS, 1);
    dma_busy = false;
    cur_buf = nullptr;
    if (pend_full && esp_ready) {               // ESP already ready again: send at once
        pend_full = false;
        start_packet(pend_buf, pend_len);
    }
    spin_unlock(disp_dma_lock, f);
}

// rising edge on DISP_PIN_READY: the ESP has armed a reception buffer
static void __not_in_flash_func(ready_irq)()
{
    if (!(gpio_get_irq_event_mask(DISP_PIN_READY) & GPIO_IRQ_EDGE_RISE)) { return; }
    gpio_acknowledge_irq(DISP_PIN_READY, GPIO_IRQ_EDGE_RISE);
    uint32_t f = spin_lock_blocking(disp_dma_lock);
    esp_ready = true;
    if (link_down) {                            // the ESP is back: its state is unknown
        link_down = false;
        state_known = false;
    }
    if (!dma_busy && pend_full) {
        pend_full = false;
        start_packet(pend_buf, pend_len);
    }
    spin_unlock(disp_dma_lock, f);
}

int display_esp_setup()
{
    gpio_init(DISP_PIN_READY);
    gpio_set_dir(DISP_PIN_READY, GPIO_IN);
    gpio_pull_down(DISP_PIN_READY);             // low while the ESP is absent or booting
    sleep_us(10);
    esp_ready = gpio_get(DISP_PIN_READY);       // already ready: no edge will come
    gpio_add_raw_irq_handler(DISP_PIN_READY, ready_irq);
    gpio_set_irq_enabled(DISP_PIN_READY, GPIO_IRQ_EDGE_RISE, true);
    irq_set_enabled(IO_IRQ_BANK0, true);
    printf("display_esp_setup: ESP %s\n", esp_ready ? "ready" : "not ready yet");
    return 0;
}

// ---------------------------------------------------------
// send_string: sends len bytes (one packet) by DMA
// - ESP ready and nothing being sent: starts now
// - otherwise, if no packet is waiting: stored, sent by interrupt
// - otherwise: polls (sound kept running) until the waiting packet has left
// s must not be modified until lnk_buffer_free(s) is true
// returns false if the packet was dropped (ESP not answering)
// ---------------------------------------------------------
bool send_string(const uint8_t* s, uint16_t len)
{
    uint64_t t0 = time_us_64();
    while (true) {
        uint32_t f = spin_lock_blocking(disp_dma_lock);
        if (!dma_busy && esp_ready) {
            start_packet(s, len);
            spin_unlock(disp_dma_lock, f);
            return true;
        }
        if (!pend_full) {
            pend_buf = s;
            pend_len = len;
            pend_full = true;
            spin_unlock(disp_dma_lock, f);
            return true;
        }
        if (link_down || time_us_64() - t0 > LNK_WAIT_TIMEOUT_US) {
            link_down = true;                   // stays down until the next READY edge
            nb_dropped++;
            spin_unlock(disp_dma_lock, f);
            return false;
        }
        spin_unlock(disp_dma_lock, f);
        sleep_us(LNK_POLL_US);
        fillVoices();
    }
}

bool lnk_buffer_free(const uint8_t* buf)
{
    uint32_t f = spin_lock_blocking(disp_dma_lock);
    bool used = (buf == cur_buf) || (pend_full && buf == pend_buf);
    spin_unlock(disp_dma_lock, f);
    return !used;
}

uint32_t lnk_dropped() { return nb_dropped; }

// ---------------------------------------------------------
// second layer: builds the orders
// ---------------------------------------------------------
static uint8_t* small_buf_get()
{
    small_idx = (small_idx + 1) % LNK_NB_SMALL_BUF;
    uint8_t* b = small_buf[small_idx];
    while (!lnk_buffer_free(b)) {               // never expected: at most 2 packets in flight
        sleep_us(LNK_POLL_US);
        fillVoices();
    }
    return b;
}

static uint8_t* put_hdr(uint8_t* p, char cmd, uint16_t n)
{
    p[0] = (uint8_t)cmd;
    p[1] = n & 0xFF;
    p[2] = n >> 8;
    return p + LNK_HDR_SIZE;
}

static uint8_t* put16(uint8_t* p, uint16_t v)
{
    p[0] = v & 0xFF;
    p[1] = v >> 8;
    return p + 2;
}

static uint8_t* put_color(uint8_t* p, uint16_t fg, uint16_t bg)
{
    if (state_known && fg == last_fg && bg == last_bg) { return p; }
    p = put_hdr(p, LNK_CMD_COLOR, 4);
    p = put16(p, fg);
    p = put16(p, bg);
    last_fg = fg; last_bg = bg;
    return p;
}

static uint8_t* put_font(uint8_t* p, uint8_t mult)
{
    if (mult < 1) { mult = 1; }
    if (mult > LNK_FONT_MAX) { mult = LNK_FONT_MAX; }
    if (state_known && mult == last_mult) { return p; }
    p = put_hdr(p, LNK_CMD_FONT, 1);
    *p++ = mult;
    last_mult = mult;
    return p;
}

static uint8_t* put_pos(uint8_t* p, uint16_t x, uint16_t y)
{
    p = put_hdr(p, LNK_CMD_POS, 4);
    p = put16(p, x);
    return put16(p, y);
}

static uint8_t* put_text(uint8_t* p, const char* s)
{
    size_t n = strlen(s);
    if (n > LNK_TEXT_MAX) {
        n = LNK_TEXT_MAX;
        while (n > 0 && ((uint8_t)s[n] & 0xC0) == 0x80) { n--; }   // do not cut a UTF-8 character
    }
    p = put_hdr(p, LNK_CMD_TEXT, (uint16_t)n);
    memcpy(p, s, n);
    return p + n;
}

static void send_small(uint8_t* b, uint8_t* end, bool sets_state)
{
    if (!send_string(b, (uint16_t)(end - b))) {
        state_known = false;                    // dropped: the ESP did not get the new state
    }
    else if (sets_state) {
        state_known = true;
    }
}

// colours, size, position and text in one packet (K and P only when they change)
void lnk_display(uint16_t x, uint16_t y, const char* s, uint16_t fg, uint16_t bg, int8_t mult)
{
    uint8_t* b = small_buf_get();
    uint8_t* p = put_color(b, fg, bg);
    p = put_font(p, (uint8_t)mult);
    p = put_pos(p, x, y);
    p = put_text(p, s);
    send_small(b, p, true);
}

void lnk_text(const char* s)
{
    uint8_t* b = small_buf_get();
    send_small(b, put_text(b, s), false);
}

void lnk_font(uint8_t mult)
{
    uint8_t* b = small_buf_get();
    bool known = state_known;
    state_known = false;                        // force the order
    uint8_t* p = put_font(b, mult);
    state_known = known;
    send_small(b, p, false);
}

void lnk_pos(uint16_t x, uint16_t y)
{
    uint8_t* b = small_buf_get();
    send_small(b, put_pos(b, x, y), false);
}

void lnk_color(uint16_t fg, uint16_t bg)
{
    uint8_t* b = small_buf_get();
    bool known = state_known;
    state_known = false;                        // force the order
    uint8_t* p = put_color(b, fg, bg);
    state_known = known;
    send_small(b, p, false);
}

// fills the zone with bg (keeps the current fg)
void lnk_clear(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t bg)
{
    uint8_t* b = small_buf_get();
    uint8_t* p = put_color(b, state_known ? last_fg : COL_WHITE, bg);
    p = put_hdr(p, LNK_CMD_CLEAR, 8);
    p = put16(p, x);
    p = put16(p, y);
    p = put16(p, w);
    p = put16(p, h);
    send_small(b, p, false);
}

// buf: LNK_GRAPH_BUF_SIZE bytes owned by the caller; pixel i (0..LNK_GRAPH_MAX_PIX-1)
void lnk_graph_set(uint8_t* buf, uint16_t i, uint16_t x, uint16_t y, uint16_t colour)
{
    uint8_t* p = buf + LNK_HDR_SIZE + i * LNK_GRAPH_PIX_SIZE;
    p = put16(p, x);
    p = put16(p, y);
    put16(p, colour);
}

void lnk_graph_send(uint8_t* buf, uint16_t nb_pix)
{
    if (nb_pix > LNK_GRAPH_MAX_PIX) { nb_pix = LNK_GRAPH_MAX_PIX; }
    put_hdr(buf, LNK_CMD_GRAPH, nb_pix * LNK_GRAPH_PIX_SIZE);
    send_string(buf, LNK_HDR_SIZE + nb_pix * LNK_GRAPH_PIX_SIZE);
}

#endif // ST7789
