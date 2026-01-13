#ifndef _LEDS_H_
#define _LEDS_H_


int ledsWs2812Setup(PIO pio,uint8_t ledPin);
void ledsWs2812Test(PIO pio,int sm,uint32_t ms);

#endif //_LEDS_H_