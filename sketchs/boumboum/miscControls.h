#ifndef _MISC_H_
#define _MISC_H_

#include <stdint.h>                                            

struct Voice;                                                   // defined in frequences.h: only a pointer is needed here

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

void effects_init();

enum AdsrStates {
    ADSR_OFF,
    ADSR_ATT,
    ADSR_DEC,
    ADSR_SUS,
    ADSR_REL,
    ADSR_MAX_STATES
};



// ************************* echo ****************************

enum { ECHO_IDLE, ECHO_RUNNING, ECHO_CLEARING };

struct EchoState {
    uint8_t state;                                              // ECHO_IDLE: bufEcho is clean, nothing computed ; ECHO_CLEARING: the DMA is clearing bufEcho
    bool    clearStarted;                                       // the DMA clear of bufEcho has been started
    int32_t w;                                                  // write index in bufEcho
    int32_t dCur;                                               // delay in use (the new one during a crossfade)
    int32_t dOld;                                               // previous delay, faded out during the crossfade
    int32_t fadePos;                                            // position in the crossfade, ECHO_FADE_LEN when there is none
    int32_t fbLast;                                             // feedback reached at the end of the previous buffer
    int32_t retLast;                                            // return level reached at the end of the previous buffer
    int32_t yLast;                                              // last output step, for the interpolation back to 44100 Hz
    int32_t silent;                                             // steps of output below ECHO_SILENCE since the last send
};

void echoProcess(int32_t* vBuffer);
void setEcho(uint8_t echo);
void setVoiceEcho(Voice* v);
void setVoiceEchoCoders(Voice* v, uint16_t coder, uint16_t coderAtt);
void setEchoCoder(uint8_t echo, uint16_t coderDur, uint16_t coderDurA, uint16_t coderInj, uint16_t coderInjA, uint16_t coderRet, uint16_t coderRetA);


#endif // _MISC_H_