#ifndef _MISC_H_
#define _MISC_H_

#include <stdint.h>

void adsrInit();
void adsrHandler();
void setAdsrDurCoders(uint8_t adsr,uint8_t adsrStage,uint16_t coder,uint16_t coderAtt);   // load the coders of a stage, then compute its duration
void setAdsrDur(uint8_t adsr,uint8_t adsrStage);                                          // compute only, from the stored coders and the stored value of the input
void setAdsrLevCoders(uint8_t adsr,uint16_t coder,uint16_t coderAtt);                     // load the coders of the level, then compute it
void setAdsrLev(uint8_t adsr);                                                            // compute only
void fillDur(void);

void irq_button_init(uint8_t pin);
void touch_button_handler(uint8_t touchButtonNb,bool* touchButtonValue,volatile bool* coderTouchB);
void touch_button_init(uint8_t pin);

void lf_mixer_init();
void setLfmCoders(uint8_t lfm,uint8_t inp,uint16_t coder,uint16_t coderAtt);   // load the coders of an input (0 = general gain, 1-3 = mixed inputs), then compute the output
void setLfm(uint8_t lfm);                                                      // compute only, from the stored coders and the stored values of the inputs
void lfmScopeHandler();
void disp_lfm(uint8_t lfm);
void disp_lfm(uint8_t lfm,char* t);

enum AdsrStates {
    ADSR_OFF,
    ADSR_ATT,
    ADSR_DEC,
    ADSR_SUS,
    ADSR_REL,
    ADSR_MAX_STATES
};


#endif // _MISC_H_