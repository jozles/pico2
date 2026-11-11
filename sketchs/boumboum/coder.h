#ifndef CODER_H_
#define CODER_H_

#include <stdint.h>

/*  ********* architecture des codeurs *********

    la cinématique est locale à coder.cpp avec la structure Coders
    le tableau Coders c[] stocke les données de gestion de chaque codeur

    le pointeur volatile int16_t* coderTimerCount est valorisé via la fonction coderSetup()
    il pointe les valeurs courantes des coders
    C'est l'utilisateur qui attribue l'usage des coders

    Le paramètre CODER_NB indique le nombre de coders multiplexés
    Le paramètre CODER_BANK_NB le nombre de codeurs dans le panneau de config

    configuration physique (types de coders, câblage des switchs, ordre) : coders_cpld.def ou coders_4051.def
    onSwitchs[] (coder.cpp) : 1 quand le switch est appuyé, quel que soit son câblage
*/

struct Coders{
    uint8_t coderItStatus;            // unused since the decoding by state
    bool coderClock;                  // current physical coder clock value
    bool coderClock0;                 // previous physical coder clock value
    bool coderData;                   // current physical coder data value
    bool coderData0;                  // previous physical coder data value
    uint8_t coderState;               // last state of CK and DT: 0 = 11, 1 = 01, 2 = 00, 3 = 10 (clockwise order)
    int8_t coderAcc;                  // changes of state counted since the last detent
    int8_t coderDir;                  // direction of the last move: +1 or -1 ; 0 after a pause ; 2 after a skip from rest (full-cycle coders)
    uint32_t coderMoveTime;           // time of the last change of state
    bool coderSwitch;                 // current physical coder switch value
    uint32_t coderSwitchTime;         // time of the last switch change
    bool touchButton;                 // current physical touchButton value
    uint32_t touchButtonTime;         // time of the last touch button change
};

struct CoderPhys{                     // one line of coders_cpld.def or coders_4051.def: one position of the scan
    uint8_t coder;                    // logical number of the coder read at this position
    uint8_t touch;                    // logical number of the touch button read at this position
    uint8_t type;                     // CT_NONE, CT_HALF or CT_FULL
    uint8_t sw;                       // SW_LOW or SW_HIGH: level given by a pressed switch
    uint8_t dir;                      // DIR_NORM or DIR_REV
};

void coderInit(uint8_t ck,uint8_t data,uint8_t sw,uint8_t vc,uint8_t sel0,uint8_t sel_nb,uint8_t nb,uint16_t ctpi,uint8_t cstn);

bool coderTimerHandler();

void slow_coder_test(uint32_t ms);

void coderSetup(volatile int16_t* cTC,volatile bool* cTS,volatile bool* cTB,uint16_t* maxi,uint8_t nb);



#endif /* CODER_H_ */