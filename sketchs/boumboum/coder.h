#ifndef CODER_H_
#define CODER_H_


#ifndef MUXED_CODER
void coderInit(uint8_t pio_ck,uint8_t pio_d,uint8_t pio_sw,uint8_t vc,uint16_t ctpi,uint8_t cstn);
bool coderTimerHandler();
#endif // MUXED_CODER
#ifdef MUXED_CODER

struct Coders{
    uint8_t coderItStatus;            // coder decoding status
    bool coderClock;                  // current physical coder clock value
    bool coderClock0;                 // previous physical coder clock value
    bool coderData;                   // current physical coder data value
    bool coderData0;                  // previous physical coder data value
    bool coderSwitch;                 // current physical coder switch value
    bool coderSwitch0;                // previous physical coder switch value
};

void coderInit(uint8_t ck,uint8_t data,uint8_t sw,uint8_t vc,uint8_t sel0,uint8_t sel_nb,uint8_t nb,uint16_t ctpi,uint8_t cstn);
bool coderTimerHandler();

#endif // MUXED_CODER

void coderSetup(volatile int32_t* cTC);



#endif /* CODER_H_ */
