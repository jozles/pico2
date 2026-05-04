#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "const.h"
#include "frequences.h"
#include "miscControls.h"
#include "input_tables_management.h"
#include "util.h"

#include "hardware/sync.h"

extern int16_t   lfo_ctl_input_id[][MAX_OUTPUTS_PER_OBJ];
extern int16_t   adsr_ctl_input_id[][MAX_OUTPUTS_PER_OBJ];

extern Voice voices[MAX_VOICES];

extern int16_t   lfosOutputsValues[MAX_LFO][MAX_OUTPUTS_PER_OBJ];
extern int16_t   adsrOutputsValues[MAX_ADSR][MAX_OUTPUTS_PER_OBJ];
extern uint16_t  lfosCoderCycleR[MAX_LFO];
extern uint16_t  lfosCycleR[MAX_LFO];
extern uint16_t  lfosCoderCycleRAtt[MAX_LFO];
extern uint16_t  lfosCodersFreq[MAX_LFO];
extern uint16_t  lfosCodersAttFreq[MAX_LFO]; 

/* ************ control inputs and outputs ************* */

// each control input of each object has an unique id wich gives access to its parameters (value, source, norm, name etc)
// each object type has an [object_type]_ctl_input_id table with [object_type#][input#] elements 
// (the ids table for the inputs of every objects of this type)
// ex: lfo#2 input#3 has the id : lfo_ctl_input_id[2][3] wich is the index in the tables ctl_input_xxx[]
// the table ctl_input_id_chain[] allows to chain the inputs wich are connected to the same output for faster access

int16_t ctl_input_val[MAX_INPUTS];                       // all inputs values
int16_t ctl_input_srce[MAX_INPUTS];                      // all inputs sources# 
uint8_t ctl_input_norm[MAX_INPUTS];                      // all inputs norm type (0 nothing ; 1 lfo_freq ; 2 vce freq ; 3 rc ; 4 ampl 0-31 etc)
uint8_t ctl_input_shft[MAX_INPUTS];                      // all inputs values shift type (0 no shift ; 1 +0x8000)
uint8_t ctl_input_trig[MAX_INPUTS];                      // all inputs trig type (0 no trig ; 1 up ; 2 down ; 3 both)
int16_t ctl_input_tlev[MAX_INPUTS];                      // all inputs trig level
char    ctl_input_name[MAX_INPUTS][IN_OUT_NAME_LEN];     // all objects inputs names
uint8_t ctl_input_update_type[MAX_INPUTS];               // all objects inputs update specific job
uint8_t ctl_input_object[MAX_INPUTS];                    // all objects inputs object#

int16_t ctl_input_id_chain[MAX_INPUTS];                  // next id(input) with same output (-1/NO_LINK if nothing)

uint8_t norm_dividers[]={0,16-5,16-6,16-3};

// each control output of each object has an unique id wich gives access to the name and input link chain of the output
// each object type has an [object]_ctl_output_id table with [object_type#][output#] elements
// (the ids table for the outputs of every objects of this type)
// ex: lfo#2 output#3 has the id : lfo_ctl_output_id[2][3] wich is the index in the tables ctl_output_xxx[] 
// the table ctl_output_id_chain[] (one element per output) allows to chain the inputs wich are connected to this output for faster access
// contains first input id of the chain or -1/NO_LINK

//int16_t*  ctl_output_val[MAX_OUTPUTS];                    // all objects ptrs to outputs values
char      ctl_output_name[MAX_OUTPUTS][IN_OUT_NAME_LEN];  // all objects outputs names
int16_t   ctl_output_id_chain[MAX_OUTPUTS];               // all objects outputs chain to input (first link)

// ***** noms des entrées/sorties des objets *****

const char lfo_inputs_names[][OBJ_IO_NAME_LEN]={         
    #define X(name,text) text,
    #include "lfos_inputs_names.def"   
    #undef X   
};

const char adsr_inputs_names[][OBJ_IO_NAME_LEN]={
    #define X(name,text) text,
    #include "adsr_inputs_names.def"   
    #undef X   
};

const char voices_inputs_names[][OBJ_IO_NAME_LEN]={
    #define X(name,text) text,
    #include "vces_inputs_names.def"   
    #undef X   
};

const char lfo_outputs_names[][OBJ_IO_NAME_LEN]={
    #define X(name,text) text,
    #include "lfos_outputs_names.def"   
    #undef X   
};

const char adsr_outputs_names[][OBJ_IO_NAME_LEN]={
    #define X(name,text) text,
    #include "adsr_outputs_names.def"   
    #undef X   
};

bool init_objects_outputs(void)
{
    //memset(ctl_output_val,0x00,MAX_OUTPUTS*sizeof(int16_t*));
    memset(ctl_output_name,'-',MAX_OUTPUTS*IN_OUT_NAME_LEN);
    int16_t curr_output=1;

    for (uint8_t lfo=0;lfo<MAX_LFO;lfo++)
    {
        for(uint8_t outs=0;outs<MAX_OUTPUTS_PER_OBJ;outs++)
        {
            //ctl_output_val[curr_output]=&lfosOutputsValues[lfo][outs];   //lfo_ctl_input_id[lfo][outs];
            ctl_output_id_chain[curr_output]=NO_LINK;
            if(outs<LFO_OUTPUTS_NB){
                char buf[IN_OUT_NAME_LEN]={'L','F','O','S'};
                convIntToString(buf+4,lfo,2);
                memcpy(buf+6,&lfo_outputs_names[outs],OBJ_IO_NAME_LEN-1);
                memcpy(ctl_output_name[curr_output],buf,IN_OUT_NAME_LEN);
            }
            curr_output++;
            if(curr_output>=MAX_OUTPUTS){return false;}
        }
    }

    for (uint8_t adsr=0;adsr<MAX_ADSR;adsr++)
    {
        for(uint8_t outs=0;outs<MAX_OUTPUTS_PER_OBJ;outs++)
        {
            //ctl_output_val[curr_output]=&adsrOutputsValues[adsr][outs];
            ctl_output_id_chain[curr_output]=NO_LINK;
            if(outs<ADSR_OUTPUTS_NB){
                char buf[IN_OUT_NAME_LEN]={'A','D','S','R'};
                convIntToString(buf+4,adsr,2);
                memcpy(buf+6,&adsr_outputs_names[outs],OBJ_IO_NAME_LEN-1);
                memcpy(ctl_output_name[curr_output],buf,IN_OUT_NAME_LEN);
            }            
            curr_output++;
            if(curr_output>=MAX_OUTPUTS){return false;}
        }
    }

// ajouter ici d'autres générateurs lents  (sequencers, kbd etc) ps: les voices n'ont pas de sorties lentes

    //for (uint16_t i=0;i<MAX_OUTPUTS;i++){if(ctl_output_val[i]!=nullptr){*ctl_output_val[i]=NO_LINK;}}

    return true;
}

bool init_objects_inputs(void)
{
    memset(ctl_input_name,0x00,MAX_INPUTS*IN_OUT_NAME_LEN);
    memset(ctl_input_srce,0x00,MAX_INPUTS);
    memset(ctl_input_shft,0x00,MAX_INPUTS);
    memset(ctl_input_trig,0x00,MAX_INPUTS);
    memset(ctl_input_tlev,0x00,MAX_INPUTS);
    
    for (uint16_t i=0;i<MAX_INPUTS;i++){
        ctl_input_id_chain[i]=NO_LINK;
    }

    memcpy(ctl_input_name[0],"---",3);
    int16_t curr_input=1;

    for (uint8_t lfo=0;lfo<MAX_LFO;lfo++)
    {
        for(uint8_t ins=0;ins<MAX_INPUTS_PER_OBJ;ins++)
        {
            lfo_ctl_input_id[lfo][ins]=curr_input;
            ctl_input_id_chain[curr_input]=NO_LINK;
            ctl_input_object[curr_input]=lfo;
            switch(ins){
                case LFRQ:ctl_input_norm[curr_input]=LFO_FREQ;break;
                case LCRA:ctl_input_norm[curr_input]=LFO_CRA;break;
            }
            
            if(ins<LFO_INPUTS_NB){
                char buf[IN_OUT_NAME_LEN]={'L','F','O','S'};
                convIntToString(buf+4,lfo,2);
                memcpy(buf+6,&lfo_inputs_names[ins],OBJ_IO_NAME_LEN-1);
                memcpy(ctl_input_name[curr_input],buf,IN_OUT_NAME_LEN);
            }
            curr_input++;
            if(curr_input>=MAX_INPUTS){return false;}
        }
    }

    for (uint8_t adsr=0;adsr<MAX_ADSR;adsr++)
    {
        for(uint8_t ins=0;ins<MAX_INPUTS_PER_OBJ;ins++)
        {
            adsr_ctl_input_id[adsr][ins]=curr_input;
            ctl_input_id_chain[curr_input]=NO_LINK;
            if(ins<ADSR_INPUTS_NB){
                char buf[IN_OUT_NAME_LEN]={'A','D','S','R'};
                convIntToString(buf+4,adsr,2);
                memcpy(buf+6,&adsr_inputs_names[ins],OBJ_IO_NAME_LEN-1);
                memcpy(ctl_input_name[curr_input],buf,IN_OUT_NAME_LEN);
            }
            curr_input++;
            if(curr_input>=MAX_INPUTS){return false;}
        }
    }

    for (uint8_t vce=0;vce<MAX_VOICES;vce++)
    {
        for(uint8_t ins=0;ins<MAX_INPUTS_PER_OBJ;ins++)
        {
            voices[vce].voice_ctl_input_id[ins]=curr_input;
            ctl_input_id_chain[curr_input]=NO_LINK;
            if(ins<VOICES_INPUTS_NB){
                char buf[IN_OUT_NAME_LEN]={'V','C','E','S'};
                convIntToString(buf+4,(uint32_t)vce,2);
                memcpy(buf+6,&voices_inputs_names[ins],OBJ_IO_NAME_LEN-1);
                memcpy(ctl_input_name[curr_input],buf,IN_OUT_NAME_LEN);
            }
            curr_input++;
            if(curr_input>=MAX_INPUTS){return false;}
        }
    }

    // ajouter ici d'autres entrées  (sequencers etc)

    return true;
}  

static spin_lock_t *inputs_id__lock;

void objects_table_init()
{
    inputs_id__lock = spin_lock_init(CTL_INPUTS_LOCK);

    init_objects_inputs();
    init_objects_outputs();
}

void connect_input(uint16_t input_id, uint16_t output)
{
    uint32_t f = spin_lock_blocking(inputs_id__lock);

    // link init only (norm/shft/trig/tlev update by menu_mapping)
    ctl_input_id_chain[input_id] = NO_LINK;

    int16_t next_id = ctl_output_id_chain[output];

    if(next_id==NO_LINK)
        {ctl_output_id_chain[output]=input_id;}

    else {
        int16_t prev=next_id;

        while (next_id != NO_LINK) {            // end of chain ? 
            prev=next_id;
            next_id=ctl_input_id_chain[next_id];
            if(next_id>MAX_INPUTS || next_id<0){
                spin_unlock(inputs_id__lock, f);
                system_error("input_id overflow");}
        }        
        ctl_input_id_chain[prev]=input_id;
    }        

    spin_unlock(inputs_id__lock, f);
}

void disconnect_input(uint16_t input_id, uint16_t output)
{
    printf("@:");sleep_ms(1);
    uint32_t f = spin_lock_blocking(inputs_id__lock);
    printf("&:");sleep_ms(1);
    int16_t first = ctl_output_id_chain[output];

    // chaîne vide → rien à faire
    if (first == NO_LINK) {
        ctl_input_id_chain[input_id] = NO_LINK;
        spin_unlock(inputs_id__lock, f);
        printf("a:");sleep_ms(1);
        return;
    }
printf("b:");
    // cas 1 : le maillon à retirer est en tête
    if (first == input_id) {
        ctl_output_id_chain[output] = ctl_input_id_chain[input_id];   // nouveau head = suivant
        ctl_input_id_chain[input_id] = NO_LINK;                  
        spin_unlock(inputs_id__lock, f);
        return;
    }

    // cas 2 : maillon au milieu / fin
    int16_t prev = first;
    int16_t curr = ctl_input_id_chain[first];
   
    if(prev>MAX_INPUTS || prev<0 || curr>MAX_INPUTS || curr<0){
        spin_unlock(inputs_id__lock, f);
        system_error("input_id overflow");}

    while (curr != NO_LINK && curr != input_id) {
        prev = curr;
        curr = ctl_input_id_chain[curr];
        
        if(curr>MAX_INPUTS || curr<0){
            spin_unlock(inputs_id__lock, f);
            system_error("input_id overflow");}
    }

    if (curr == input_id) {
        // on saute le maillon courant
        ctl_input_id_chain[prev] = ctl_input_id_chain[curr];
        ctl_input_id_chain[curr] = NO_LINK;  
    }

    spin_unlock(inputs_id__lock, f);
}

void update_inputs(uint16_t output,int16_t valeur)
{
    int16_t id = ctl_output_id_chain[output];
    uint8_t lfo=0;
    uint8_t voice=0;
    int16_t val;

    if (__builtin_expect(id != NO_LINK, 0))
    {
        do {
            int16_t next = ctl_input_id_chain[id];
            switch(ctl_input_update_type[id]){
                case VCE_FREQ: voice=ctl_input_object[id];
                               ctl_input_val[id] = valeur;
                               val=(valeur>>3)*voices[voice].coderAttFreq/MAX_CTL_ATT;   // 8k max VCES_MAX_FREQ_CODERS ; att 0-255
                               setVoiceFrequency(calcFreq(val+voices[voice].coderFreq),&voices[voice],voices[voice].coderCycleR);   // ajouter un ctl d'overflow
                               break;
                case VCE_CRA : voice=ctl_input_object[id];
                               val=(valeur>>10)*voices[voice].coderCycleRAtt/MAX_CTL_ATT; // 0-62 ; att 0-255
                               ctl_input_val[id] = val;                
                               voices[voice].cycleR = val+voices[voice].coderCycleR;
                               break;
                case SND_AMPL: break;
                case LFO_FREQ: lfo=ctl_input_object[id];
                               ctl_input_val[id] = valeur;
                               val=(valeur>>3)*lfosCodersAttFreq[lfo]/MAX_CTL_ATT;   // 8k max VCES_MAX_FREQ_CODERS ; att 0-255                
                               setLfosFrequency(calcFreq(val+lfosCodersFreq[lfo]),lfo,lfosCoderCycleR[lfo]);    // ajouter un ctl d'overflow
                               break;
                case LFO_CRA : lfo=ctl_input_object[id];
                               val=(valeur>>10)*lfosCoderCycleRAtt[lfo]/MAX_CTL_ATT; // 0-62 ; att 0-255
                               ctl_input_val[id] = val;
                               lfosCycleR[lfo]= val+lfosCoderCycleR[lfo];
                               break;
                default: break;
            }
            id = next;
        } while (id != NO_LINK);
    }
}