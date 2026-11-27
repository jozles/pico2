#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "pico/stdlib.h"
#include "const.h"
#include "sequencer.h"
#include "util.h"
#include "miscControls.h"
#include "input_tables_management.h"
#include "frequences.h"
#include "hardware/sync.h"
#include "hardware/dma.h"


uint16_t    seqCoderGLev[MAX_SEQ];
uint16_t    seqCoderGLevAtt[MAX_SEQ];
uint16_t    seqCoderStepLev[MAX_SEQ][SEQ_CHANNEL_NB][SEQ_STEP_NB];
uint16_t    seqCoderMode[MAX_SEQ];                               // saisie seqMode
uint16_t    seqCoderStepsNb[MAX_SEQ];                            // saisie seqStepsNb

uint8_t     seqMode[MAX_SEQ];                                    // 1 up, 2 up/down, 3 down, 4 random up, 5 random up/down, 6 random down etc... un bit supplémentaire indique one way  
uint8_t     seqCtl[MAX_SEQ];                                     // 0 compteur arrêté pas de gate 
uint8_t     seqChanCtl[MAX_SEQ][SEQ_CHANNEL_NB];                 // 0 channel arrêté pas de gate ; 1 suit le compteur
uint16_t    seqStepCtl[MAX_SEQ][SEQ_CHANNEL_NB][SEQ_STEP_NB];    // 0 masque la sortie de la gate ; 1 gate sort
uint8_t     seqStepsNb[MAX_SEQ];                                 // valeur maxi pour le compteur

uint8_t     seqGLev[MAX_SEQ];                                    // effective GLevel index f(coder,modulation), 0..SEQ_MAX_LEVEL_CODERS
uint16_t    seqStepLevel[MAX_SEQ][SEQ_CHANNEL_NB][SEQ_STEP_NB];  // effective level index g(Glevel,coderStep)

int16_t     seq_ctl_input_id[MAX_SEQ][SEQ_INPUTS_NB];
int16_t     seq_ctl_output_id[MAX_SEQ][SEQ_OUTPUTS_NB];


/* *******  extern  ******** */

extern volatile uint32_t millisCounter;
extern bool             setupComplete;
extern uint16_t         amplLevel[];
extern int16_t          ctl_input_val[TOTAL_INPUTS_ID_REAL];
extern int16_t          ctl_output_id_chain[];
extern const uint32_t   yTableQ16[];


// shift applied to the 16-bit input value before it is added to the coder (the input is then attenuated by coderXxxAtt/256):
// 15 - log2(number of steps of the target scale): 10 for the 32-step level scale
#define SEQ_LEV_SHIFT  10      // level: 32 steps (the old shift of 3 was far too small for this scale)

void __not_in_flash_func(setSeqLevCoders)(uint8_t seq,uint16_t coderG,uint16_t coderGAtt) // seqGLev=f(GLev,GLevAtt)
{
    const uint32_t irq=save_and_disable_interrupts();
    seqCoderGLev[seq]=coderG;
    seqCoderGLevAtt[seq]=coderGAtt;
    seqGLev[seq]=ctlCombine(coderG,coderGAtt,ctl_input_val[seq_ctl_input_id[seq][SEQ_LV]],SEQ_LEV_SHIFT,0,SEQ_MAX_LEVEL_CODERS);
    restore_interrupts(irq);
}

void __not_in_flash_func(setSeqStepCoder)(uint8_t seq,uint8_t chan,uint8_t step,uint16_t c)    // seqStepLev=f(seqGLev,stepLev)
{
    const uint32_t irq=save_and_disable_interrupts();
    seqCoderStepLev[seq][chan][step]=c;
    seqStepLevel[seq][chan][step]=0;    //???;
    restore_interrupts(irq);
}