#ifndef CODER_H_
#define CODER_H_


#ifndef MUXED_CODER
void coderInit(uint8_t pio_ck,uint8_t pio_d,uint8_t pio_sw,uint8_t vc,uint16_t ctpi,uint8_t cstn);
bool coderTimerHandler();
#endif // MUXED_CODER
#ifdef MUXED_CODER

void coderInit(Coders* c,uint8_t ck,uint8_t data,uint8_t sw,uint8_t vc,uint8_t sel0,uint8_t sel_nb,uint8_t nb,uint16_t ctpi,uint8_t cstn);
bool coderTimerHandler(Coders* c);
struct Coders{
    uint8_t coderItStatus[CODER_NB];            // coder decoding status
    bool coderClock[CODER_NB];                  // current physical coder clock value
    bool coderClock0[CODER_NB];                 // previous physical coder clock value
    bool coderData[CODER_NB];                   // current physical coder data value
    bool coderData0[CODER_NB];                  // previous physical coder data value
    bool coderSwitch[CODER_NB];                 // current physical coder switch value
}
#endif // MUXED_CODER

void coderSetup(volatile int32_t* cTC);

void show_cnt();



#endif /* CODER_H_ */
