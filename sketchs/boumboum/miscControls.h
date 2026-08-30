#ifndef _MISC_H_
#define _MISC_H_

#include <stdint.h>

void adsrInit();
void adsrHandler();
void setAdsrDur(uint8_t adsr,uint8_t adsrStatus,int32_t val);
void setAdsrLev(uint8_t adsr,int32_t val);
void fillDur(void);

void irq_button_init(uint8_t pin);
void touch_button_handler(uint8_t touchButtonNb,bool* touchButtonValue,volatile bool* coderTouchB);
void touch_button_init(uint8_t pin);

void lf_mixer_init();
void setLfm(uint8_t lfm,uint8_t inp,int16_t val);

enum AdsrStates {
    ADSR_OFF,
    ADSR_ATT,
    ADSR_DEC,
    ADSR_SUS,
    ADSR_REL,
    ADSR_MAX_STATES
};


#endif // _MISC_H_