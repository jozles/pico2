#ifndef _SEQ_H
#define _SEQ_H

#include <stdint.h>

struct Sequencer{
    uint8_t     mode;
    uint8_t     status;
    uint8_t     chanStatus[SEQ_CHAN_NB];
    uint16_t    level[SEQ_CHAN_NB][SEQ_STEP_NB];
    uint8_t     steMode[SEQ_STEP_NB];

    int16_t     ctl_input_id[SEQ_INPUT_NB];

};


#endif // _SEQ_H