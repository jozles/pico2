#ifndef _MISC_H_
#define _MISC_H_

void adsrInit();
void adsrHandler();
void setAdsrDur(uint8_t adsr,uint8_t adsrStatus,int32_t val);
void setAdsrLev(uint8_t adsr,int32_t val);
void fillDur(void);

enum AdsrStates {
    ADSR_OFF,
    ADSR_ATT,
    ADSR_DEC,
    ADSR_SUS,
    ADSR_REL,
    ADSR_MAX_STATES
};


#endif // _MISC_H_