#ifndef _LEDS_H_
#define _LEDS_H_


int ledsWs2812Setup(PIO pio,uint8_t ledPin);
volatile bool get_ws_dma_done();
void ws_dma_irq_handler();
void ws_show_3(uint32_t ms);
#ifdef GLOBAL_DMA_IRQ_HANDLER 
void ws_dma_irq_handler();
#endif
#endif //_LEDS_H_