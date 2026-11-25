#ifndef _LEDS_H_
#define _LEDS_H_

#include <stdint.h>

int ledsWs2812Setup(PIO pio,uint8_t ledPin);
volatile bool get_ws_dma_done();        // pour debug
void ws_dma_irq_handler();
void ws_show_3(uint32_t ms);
void bb_leds_handler();
bool setWsLeds(const uint8_t* leds, uint8_t pos, uint8_t len, const bool* modifs);

#endif //_LEDS_H_