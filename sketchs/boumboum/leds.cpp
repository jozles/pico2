#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/clocks.h"

#include "ws2812.pio.h"

#define LED_PIN 2
#define BUFFER_WORDS 64   // paramétrable

static uint32_t buffer[BUFFER_WORDS];
static PIO pio = pio0;
static int sm;
static int dma_chan;

void fill_buffer(uint32_t *buf, size_t n);

// IRQ DMA → buffer terminé
void __isr dma_handler() {
    dma_hw->ints0 = 1u << dma_chan;

    // ligne à 0 pendant le reset WS2812
    gpio_put(LED_PIN, 0);
    sleep_us(60);

    fill_buffer(buffer, BUFFER_WORDS);

    // relance DMA
    dma_channel_set_read_addr(dma_chan, buffer, false);
    dma_channel_set_trans_count(dma_chan, BUFFER_WORDS, true);
}

int main() {
    stdio_init_all();

    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, 0);

    uint offset = pio_add_program(pio, &ws2812_program);
    sm = pio_claim_unused_sm(pio, true);

    pio_sm_config c = ws2812_program_get_default_config(offset);
    sm_config_set_sideset_pins(&c, LED_PIN);
    pio_sm_set_consecutive_pindirs(pio, sm, LED_PIN, 1, true);

    // PIO clock = 8 MHz
    float div = (float)clock_get_hz(clk_sys) / 8000000.0f;
    sm_config_set_clkdiv(&c, div);

    sm_config_set_out_shift(&c, true, true, 32);

    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_enabled(pio, sm, true);

    // DMA
    dma_chan = dma_claim_unused_channel(true);
    dma_channel_config dc = dma_channel_get_default_config(dma_chan);
    channel_config_set_transfer_data_size(&dc, DMA_SIZE_32);
    channel_config_set_read_increment(&dc, true);
    channel_config_set_write_increment(&dc, false);
    channel_config_set_dreq(&dc, pio_get_dreq(pio, sm, true));

    dma_channel_configure(
        dma_chan, &dc,
        &pio->txf[sm],
        buffer,
        BUFFER_WORDS,
        false
    );

    dma_hw->ints0 = 1u << dma_chan;
    dma_channel_set_irq0_enabled(dma_chan, true);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_handler);
    irq_set_enabled(DMA_IRQ_0, true);

    fill_buffer(buffer, BUFFER_WORDS);

    dma_channel_start(dma_chan);

    while (true) tight_loop_contents();
}

void fill_buffer(uint32_t *buf, size_t n) {
    for (size_t i = 0; i < n; i++) {
        uint8_t r = (i * 5) & 0xFF;
        uint8_t g = (i * 3) & 0xFF;
        uint8_t b = (i * 7) & 0xFF;

        buf[i] = (g << 16) | (r << 8) | b;  // format GRB
    }
}