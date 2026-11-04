#include "const.h"

#ifndef ST7789

// ---------------------------------------------------------
// display through the ESP32 board: UART + DMA, CTS flow control
// packet = LNK_SYNC, length of the orders (2 bytes, low first), orders, sum of the order bytes
// order  = letter, n (2 bytes, low first), n data bytes
// ---------------------------------------------------------
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "hardware/dma.h"
#include "hardware/sync.h"
#include "display_esp.h"
#include "frequences.h"
#include "util.h"

extern int                disp_dma_chan;    // screen.cpp
extern dma_channel_config disp_dma_cfg;     // screen.cpp
extern spin_lock_t*       disp_dma_lock;    // screen.cpp

static uint32_t          link_baud;
static volatile bool     dma_busy = false;      // a packet is being sent
static volatile bool     link_down = false;     // the ESP did not answer: drop instead of waiting
static volatile bool     pend_full = false;     // a packet is waiting for the current one
static const uint8_t* volatile pend_buf;
static volatile uint16_t pend_len;
static const uint8_t* volatile cur_buf = nullptr; // packet being sent
static volatile uint32_t nb_dropped = 0;

static uint8_t small_buf[LNK_NB_SMALL_BUF][LNK_SMALL_BUF_SIZE] __attribute__((aligned(4)));
static uint8_t small_idx = 0;

static bool     state_known = false;            // last colours/multiplier received by the ESP
static uint16_t last_fg, last_bg;
static uint8_t  last_mult;

// ---------------------------------------------------------
// low level
// ---------------------------------------------------------
static void uart_setup()                        // also empties the UART FIFO (block reset)
{
    link_baud = uart_init(DISP_UART, link_baud);
    uart_set_format(DISP_UART, 8, 1, UART_PARITY_NONE);
    uart_set_hw_flow(DISP_UART, true, false);   // CTS: the ESP stops us when it is full
    uart_set_fifo_enabled(DISP_UART, true);
}

// must be called with disp_dma_lock held
static void __not_in_flash_func(start_packet)(const uint8_t* buf, uint16_t len)
{
    dma_busy = true;
    cur_buf = buf;
    dma_channel_configure(disp_dma_chan, &disp_dma_cfg, &uart_get_hw(DISP_UART)->dr, buf, len, true);
}

// must be called with disp_dma_lock held: stops the transfer, drops the packets
static void link_abort()
{
    dma_channel_set_irq1_enabled(disp_dma_chan, false);
    dma_channel_abort(disp_dma_chan);
    dma_hw->ints1 = 1u << disp_dma_chan;        // drop a completion flag raised by the abort
    dma_channel_set_irq1_enabled(disp_dma_chan, true);
    uart_setup();                               // the bytes left in the FIFO must not reach the ESP
    nb_dropped += 1 + (pend_full ? 1 : 0);
    dma_busy = false;
    cur_buf = nullptr;
    pend_full = false;
    link_down = true;
    state_known = false;
}

// end of DMA: the last bytes are in the UART FIFO, the next packet can follow them
void __not_in_flash_func(esp_dma_end)()
{
    uint32_t f = spin_lock_blocking(disp_dma_lock);
    dma_busy = false;
    cur_buf = nullptr;
    if (pend_full) {
        pend_full = false;
        start_packet(pend_buf, pend_len);
    }
    spin_unlock(disp_dma_lock, f);
}

int display_esp_setup(uint32_t baud)
{
    link_baud = baud;
    uart_setup();
    gpio_set_function(DISP_PIN_TX, GPIO_FUNC_UART);
    gpio_set_function(DISP_PIN_CTS, GPIO_FUNC_UART);
    gpio_pull_up(DISP_PIN_CTS);                 // ESP absent or off: not ready
    printf("display_esp_setup: UART %lu bit/s, ESP %s\n", (unsigned long)link_baud,
           gpio_get(DISP_PIN_CTS) ? "not ready" : "ready");
    return 0;
}

// ---------------------------------------------------------
// send_string: sends len bytes (one complete packet) by DMA
// - nothing being sent: starts now
// - otherwise, if no packet is waiting: stored, sent at the end of the current one
// - otherwise: polls (sound kept running) until the waiting packet has left
// - ESP silent for LNK_WAIT_TIMEOUT_US: packets dropped until it is ready again (CTS low)
// s must not be modified until lnk_buffer_free(s) is true
// returns false if the packet was dropped
// ---------------------------------------------------------
bool send_string(const uint8_t* s, uint16_t len)
{
    uint64_t t0 = time_us_64();
    while (true) {
        uint32_t f = spin_lock_blocking(disp_dma_lock);
        if (link_down) {
            if (gpio_get(DISP_PIN_CTS)) {       // still not ready
                nb_dropped++;
                spin_unlock(disp_dma_lock, f);
                return false;
            }
            link_down = false;
        }
        if (!dma_busy) {
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
        if (time_us_64() - t0 > LNK_WAIT_TIMEOUT_US) {
            link_abort();
            nb_dropped++;                       // this one too
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
// second layer: builds the packets
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

static uint8_t* pkt_begin(uint8_t* b)
{
    b[0] = LNK_SYNC;
    return b + LNK_PKT_HDR;
}

// writes the length and the sum; returns the size of the whole packet
static uint16_t pkt_end(uint8_t* b, uint8_t* end)
{
    uint16_t n = (uint16_t)(end - (b + LNK_PKT_HDR));
    uint8_t sum = 0;
    for (uint8_t* p = b + LNK_PKT_HDR; p < end; p++) { sum += *p; }
    b[1] = n & 0xFF;
    b[2] = n >> 8;
    *end = sum;
    return n + LNK_PKT_HDR + 1;
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

static uint8_t* put_font(uint8_t* p, int8_t mult)
{
    if (mult < 1) { mult = 1; }
    if (mult > LNK_FONT_MAX) { mult = LNK_FONT_MAX; }
    if (state_known && (uint8_t)mult == last_mult) { return p; }
    p = put_hdr(p, LNK_CMD_FONT, 1);
    *p++ = (uint8_t)mult;
    last_mult = (uint8_t)mult;
    return p;
}

static uint8_t* put_pos(uint8_t* p, uint16_t x, uint16_t y)
{
    p = put_hdr(p, LNK_CMD_POS, 4);
    p = put16(p, x);
    return put16(p, y);
}

// cmd: LNK_CMD_TEXT, LNK_CMD_UP or LNK_CMD_DOWN
static uint8_t* put_text(uint8_t* p, char cmd, const char* s)
{
    size_t n = strlen(s);
    if (n > LNK_TEXT_MAX) {
        n = LNK_TEXT_MAX;
        while (n > 0 && ((uint8_t)s[n] & 0xC0) == 0x80) { n--; }   // do not cut a UTF-8 character
    }
    p = put_hdr(p, cmd, (uint16_t)n);
    memcpy(p, s, n);
    return p + n;
}

static void send_small(uint8_t* b, uint8_t* end, bool sets_state)
{
    if (!send_string(b, pkt_end(b, end))) {
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
    uint8_t* p = pkt_begin(b);
    p = put_color(p, fg, bg);
    p = put_font(p, mult);
    p = put_pos(p, x, y);
    p = put_text(p, LNK_CMD_TEXT, s);
    send_small(b, p, true);
}

void lnk_text(const char* s)
{
    uint8_t* b = small_buf_get();
    send_small(b, put_text(pkt_begin(b), LNK_CMD_TEXT, s), false);
}

void lnk_font(uint8_t mult)
{
    uint8_t* b = small_buf_get();
    bool known = state_known;
    state_known = false;                        // force the order
    uint8_t* p = put_font(pkt_begin(b), (int8_t)mult);
    state_known = known;
    send_small(b, p, false);
}

void lnk_pos(uint16_t x, uint16_t y)
{
    uint8_t* b = small_buf_get();
    send_small(b, put_pos(pkt_begin(b), x, y), false);
}

void lnk_color(uint16_t fg, uint16_t bg)
{
    uint8_t* b = small_buf_get();
    bool known = state_known;
    state_known = false;                        // force the order
    uint8_t* p = put_color(pkt_begin(b), fg, bg);
    state_known = known;
    send_small(b, p, false);
}

// fills the zone with bg (keeps the current fg)
void lnk_clear(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t bg)
{
    uint8_t* b = small_buf_get();
    uint8_t* p = put_color(pkt_begin(b), state_known ? last_fg : COL_WHITE, bg);
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
    uint8_t* p = buf + LNK_PKT_HDR + LNK_HDR_SIZE + i * LNK_GRAPH_PIX_SIZE;
    p = put16(p, x);
    p = put16(p, y);
    put16(p, colour);
}

void lnk_graph_send(uint8_t* buf, uint16_t nb_pix)
{
    if (nb_pix > LNK_GRAPH_MAX_PIX) { nb_pix = LNK_GRAPH_MAX_PIX; }
    uint8_t* p = put_hdr(pkt_begin(buf), LNK_CMD_GRAPH, nb_pix * LNK_GRAPH_PIX_SIZE);
    send_string(buf, pkt_end(buf, p + nb_pix * LNK_GRAPH_PIX_SIZE));
}

// scrolling zone over the full width: nb_lines lines of pitch pixels from y, cleared with bg
void lnk_scroll_zone(uint16_t y, uint8_t nb_lines, uint8_t pitch, uint16_t bg)
{
    uint8_t* b = small_buf_get();
    uint8_t* p = put_color(pkt_begin(b), state_known ? last_fg : COL_WHITE, bg);
    p = put_hdr(p, LNK_CMD_ZONE, 4);
    p = put16(p, y);
    *p++ = nb_lines;
    *p++ = pitch;
    send_small(b, p, false);
}

// the zone moves up one line, s is written on the new bottom line
void lnk_scroll_up(const char* s, uint16_t fg, uint16_t bg, int8_t mult)
{
    uint8_t* b = small_buf_get();
    uint8_t* p = put_color(pkt_begin(b), fg, bg);
    p = put_font(p, mult);
    p = put_text(p, LNK_CMD_UP, s);
    send_small(b, p, true);
}

// the zone moves down one line, s is written on the new top line
void lnk_scroll_down(const char* s, uint16_t fg, uint16_t bg, int8_t mult)
{
    uint8_t* b = small_buf_get();
    uint8_t* p = put_color(pkt_begin(b), fg, bg);
    p = put_font(p, mult);
    p = put_text(p, LNK_CMD_DOWN, s);
    send_small(b, p, true);
}

#endif // ST7789
