#ifndef _MISC_H_
#define _MISC_H_

void adsrInit();
void adsrHandler();
void setAdsrDur(int32_t val,uint16_t* what);
void setAdsrLev(int32_t val,uint8_t adsr);

enum AdsrStates {
    ADSR_OFF,
    ADSR_ATT,
    ADSR_DEC,
    ADSR_SUS,
    ADSR_REL,
    ADSR_MAX_STATES
};


#endif // _MISC_H_