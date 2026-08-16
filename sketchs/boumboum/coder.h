#ifndef CODER_H_
#define CODER_H_

/*  ********* architecture des codeurs *********

    la cinématique est locale à coder.cpp avec la structure Coders
    le tableau Coders c[] stocke les données de gestion de chaque codeur

    le pointeur volatile int32_t* coderTimerCount est valorisé via la fonction setup() 
    il pointe les valeurs courantes des coders
    C'est l'utilisateur qui attribue l'usage des coders

    Le paramètre CODER_NB indique le nombre de coders multiplexés
    Le paramètre CODER_BANK_NB le nombre de codeurs dans le panneau de config
*/

struct Coders{
    uint8_t coderItStatus;            // coder decoding status
    bool coderClock;                  // current physical coder clock value
    bool coderClock0;                 // previous physical coder clock value
    bool coderData;                   // current physical coder data value
    bool coderData0;                  // previous physical coder data value
    bool coderSwitch;                 // current physical coder switch value
    uint32_t coderSwitchTime;         // previous physical coder switch time
    bool touchButton;                 // current physical touchButton value
    uint32_t touchButtonTime;         // previous physical touchButton time
};

void coderInit(uint8_t ck,uint8_t data,uint8_t sw,uint8_t vc,uint8_t sel0,uint8_t sel_nb,uint8_t nb,uint16_t ctpi,uint8_t cstn);
bool coderTimerHandler();

void slow_coder_test(uint32_t ms);

void coderSetup(volatile int16_t* cTC,volatile bool* cTS,volatile bool (*cTB)[OUTPUTS_STATES_NB],uint16_t* maxi,uint8_t nb);



#endif /* CODER_H_ */
