#ifndef _LEDS_H_
#define _LEDS_H_


int ledsWs2812Setup(PIO pio,uint8_t ledPin);
void ledsWs2812Test(uint32_t ms);
#ifdef GLOBAL_DMA_IRQ_HANDLER 
void ws_dma_irq_handler();
#endif
#endif //_LEDS_H_