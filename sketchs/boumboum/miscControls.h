#ifndef _MISC_H_
#define _MISC_H_

void adsrInit();
void adsrHandler();

enum AdsrStates {
    ADSR_OFF,
    ADSR_ATT,
    ADSR_DEC,
    ADSR_SUS,
    ADSR_REL,
    ADSR_MAX_STATES
};




#endif // _MISC_H_