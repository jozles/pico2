#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "ws2812.pio.h"
#include "const.h"

static PIO pio = pio1;     // <-- PIO1 = propre, libre, fiable
static uint sm;
static uint offset;

void ledsWs2812Setup(void) {

    printf("WS2812 program length = %d\n", ws2812_program.length);

    // 1. Claim une SM libre
    sm = pio_claim_unused_sm(pio, true);
    printf("SM claim = %u\n", sm);

    // 2. Charger le programme WS2812 dans PIO1
    offset = pio_add_program(pio, &ws2812_program);
    printf("offset = %u\n", offset);

    // 3. Configurer la pin
    pio_gpio_init(pio, LED_PIN_WS2812);

    // 4. Configurer la SM
    pio_sm_config c = ws2812_program_get_default_config(offset);

    sm_config_set_sideset_pins(&c, LED_PIN_WS2812);
    sm_config_set_out_pins(&c, LED_PIN_WS2812, 1);

    sm_config_set_out_shift(&c, true, true, 24);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);

    float div = (float)clock_get_hz(clk_sys) / 800000.0f;
    sm_config_set_clkdiv(&c, div);

    // 5. Init + enable
    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_enabled(pio, sm, true);

    printf("WS2812 ready on PIO1 SM=%u PIN=%d\n", sm, LED_PIN_WS2812);
}

void ledsWs2812Test(void) {
    while (true) {
        pio_sm_put_blocking(pio, sm, 0x00FF00); // vert
        sleep_ms(500);

        pio_sm_put_blocking(pio, sm, 0x0000FF); // bleu
        sleep_ms(500);

        pio_sm_put_blocking(pio, sm, 0xFF0000); // rouge
        sleep_ms(500);

        pio_sm_put_blocking(pio, sm, 0xFFFFFF); // blanc
        sleep_ms(500);

        pio_sm_put_blocking(pio, sm, 0x000000); // off
        sleep_ms(500);
    }
}









/*#include "pico/stdlib.h"
#include <stdio.h>
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/clocks.h"
#include "const.h"
#include "util.h"

extern "C" {
    #include "ws2812.pio.h"
}

// leds

extern volatile uint32_t durOffOn[];
extern volatile bool led;
extern volatile uint32_t ledBlinker;

// millis

extern volatile uint32_t millisCounter;

#define BUFFER_WORDS 64   // paramétrable

static uint32_t buffer[BUFFER_WORDS];
static PIO pio = pio0;
static int sm;
static int dma_chan;

void fill_buffer(uint32_t *buf, size_t n);

// IRQ DMA → buffer terminé
void __isr dma_handler() {

//printf("IRQ DMA OK\n");
//sleep_ms(10);

    dma_hw->ints0 = 1u << dma_chan;

    // ligne à 0 pendant le reset WS2812
    gpio_put(LED_PIN_WS2812, 0);
    sleep_us(60);

    fill_buffer(buffer, BUFFER_WORDS);

    // relance DMA
    dma_channel_set_read_addr(dma_chan, buffer, false);
    dma_channel_set_trans_count(dma_chan, BUFFER_WORDS, true);
}

int ledsWs2812Setup() {
    //stdio_init_all();

    printf("\nledsWsSetup \n"); sleep_ms(1000);

    //gpio_init(LED_PIN_WS2812);
    //gpio_set_dir(LED_PIN_WS2812, GPIO_OUT);
    //gpio_put(LED_PIN_WS2812, 0);

    uint offset = pio_add_program(pio, &ws2812_program);
    sm = pio_claim_unused_sm(pio, true);

    pio_sm_config c = ws2812_program_get_default_config(offset);

sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
pio_gpio_init(pio,LED_PIN_WS2812);
    sm_config_set_sideset_pins(&c, LED_PIN_WS2812);
    pio_sm_set_consecutive_pindirs(pio, sm, LED_PIN_WS2812, 1, true);

    // PIO clock = 8 MHz
    float div = (float)clock_get_hz(clk_sys) / 8000000.0f;
    sm_config_set_clkdiv(&c, div);

    sm_config_set_out_shift(&c, true, true, 32);

    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_enabled(pio, sm, true);

sleep_ms(10);
printf("FLEVEL = %d\n", pio_sm_get_tx_fifo_level(pio, sm));
pio_sm_put_blocking(pio, sm, 0x00FF00);  // vert
sleep_ms(10);

    // DMA
    dma_chan = dma_claim_unused_channel(true);
    if(dma_chan<0){LEDBLINK_ERROR_DMA}
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

// Effacer toute interruption DMA en attente
dma_hw->ints2 = 1u << dma_chan;

// Activer l'interruption pour ce canal
dma_hw->inte2 |= (1u << dma_chan);

// Attacher le handler global DMA_IRQ_2
irq_set_exclusive_handler(DMA_IRQ_2, dma_handler);
irq_set_enabled(DMA_IRQ_2, true);

    return 0;
}

void ledsWs2812Test()
{
    fill_buffer(buffer, BUFFER_WORDS);

pio_sm_put_blocking(pio, sm, buffer[0]);

    dma_channel_start(dma_chan);

    sleep_ms(5);
printf("FLEVEL after DMA = %d\n", pio_sm_get_tx_fifo_level(pio, sm));

    //printf("\nledsWsTest \n"); 
    sleep_ms(10);

    while (true) tight_loop_contents();
}

void fill_buffer(uint32_t *buf, size_t n) {

for (int i = 0; i < BUFFER_WORDS; i++) {
    buffer[i] = 0x00FF00;   // vert (GRB)
}

}*/