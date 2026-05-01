#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "const.h"
#include "frequences.h"
#include "miscControls.h"
#include "input_tables_management.h"
#include "util.h"

#include "hardware/sync.h"

extern int16_t   lfo_in_table_id[][MAX_OUTPUTS_PER_OBJ];
extern int16_t   adsr_in_table_id[][MAX_OUTPUTS_PER_OBJ];

extern Voice voices[MAX_VOICES];

// each input of each object has an unique id wich give the value, the source, the norm and the name
// each object has an [object]_in_table_id table with [object#][input#] elements (the ids)
// so the id is available from the object/input to access its value,source,norm,name

int16_t in_table_val[MAX_INPUTS];                       // all objects inputs values
int16_t in_table_id[MAX_INPUTS];                        // chain : next id(input) with same output (-1/NO_LINK if nothing)
int16_t in_table_srce[MAX_INPUTS];                      // all objects inputs sources# 
int16_t in_table_type_norm[MAX_INPUTS];                 // all objects norm type values
char    in_table_name[MAX_INPUTS][IN_OUT_NAME_LEN];     // all objects inputs names

// each output of each object has an unique id wich give the value ptr and the name
// each object has an [object]_out_table_id table with [object#][output#] elements (the ids)
// so the id is available from the object/output to access its value,names

static int16_t*  out_table_val[MAX_OUTPUTS];            // all objects ptrs to outputs values
char    out_table_name[MAX_OUTPUTS][IN_OUT_NAME_LEN];   // all objects outputs names

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
    memset(out_table_val,0x00,MAX_OUTPUTS*sizeof(int16_t*));
    memset(out_table_name,'-',MAX_OUTPUTS*IN_OUT_NAME_LEN);
    int16_t curr_output=1;

    for (uint8_t lfo=0;lfo<MAX_LFO;lfo++)
    {
        for(uint8_t outs=0;outs<MAX_OUTPUTS_PER_OBJ;outs++)
        {
            out_table_val[curr_output]=&lfo_in_table_id[lfo][outs];
            if(outs<LFO_OUTPUTS_NB){
                char buf[IN_OUT_NAME_LEN]={'L','F','O','S'};
                convIntToString(buf+4,lfo,2);
                memcpy(buf+6,&lfo_outputs_names[outs],OBJ_IO_NAME_LEN-1);
                memcpy(out_table_name[curr_output],buf,IN_OUT_NAME_LEN);
            }
            curr_output++;
            if(curr_output>=MAX_OUTPUTS){return false;}
        }
    }

    for (uint8_t adsr=0;adsr<MAX_ADSR;adsr++)
    {
        for(uint8_t outs=0;outs<MAX_OUTPUTS_PER_OBJ;outs++)
        {
            out_table_val[curr_output]=&adsr_in_table_id[adsr][outs];
            if(outs<ADSR_OUTPUTS_NB){
                char buf[IN_OUT_NAME_LEN]={'A','D','S','R'};
                convIntToString(buf+4,adsr,2);
                memcpy(buf+6,&adsr_outputs_names[outs],OBJ_IO_NAME_LEN-1);
                memcpy(out_table_name[curr_output],buf,IN_OUT_NAME_LEN);
            }            
            curr_output++;
            if(curr_output>=MAX_OUTPUTS){return false;}
        }
    }

// ajouter ici d'autres générateurs lents  (sequencers, kbd etc) ps: les voices n'ont pas de sorties lentes

    for (uint16_t i=0;i<MAX_OUTPUTS;i++){if(out_table_val[i]!=nullptr){*out_table_val[i]=NO_LINK;}}

    return true;
}

bool init_objects_inputs(void)
{
    memset(in_table_name,0x00,MAX_INPUTS*IN_OUT_NAME_LEN);
    memset(in_table_srce,0x00,MAX_INPUTS);
    
    for (uint16_t i=0;i<MAX_INPUTS;i++){
        in_table_id[i]=NO_LINK;
    }

    memcpy(in_table_name[0],"---",3);
    int16_t curr_input=1;

    for (uint8_t lfo=0;lfo<MAX_LFO;lfo++)
    {
        for(uint8_t ins=0;ins<MAX_INPUTS_PER_OBJ;ins++)
        {
            lfo_in_table_id[lfo][ins]=curr_input;
            if(ins<LFO_INPUTS_NB){
                char buf[IN_OUT_NAME_LEN]={'L','F','O','S'};
                convIntToString(buf+4,lfo,2);
                memcpy(buf+6,&lfo_inputs_names[ins],OBJ_IO_NAME_LEN-1);
                memcpy(in_table_name[curr_input],buf,IN_OUT_NAME_LEN);
            }
            curr_input++;
            if(curr_input>=MAX_INPUTS){return false;}
        }
    }

    for (uint8_t adsr=0;adsr<MAX_ADSR;adsr++)
    {
        for(uint8_t ins=0;ins<MAX_INPUTS_PER_OBJ;ins++)
        {
            adsr_in_table_id[adsr][ins]=curr_input;
            if(ins<ADSR_INPUTS_NB){
                char buf[IN_OUT_NAME_LEN]={'A','D','S','R'};
                convIntToString(buf+4,adsr,2);
                memcpy(buf+6,&adsr_inputs_names[ins],OBJ_IO_NAME_LEN-1);
                memcpy(in_table_name[curr_input],buf,IN_OUT_NAME_LEN);
            }
            curr_input++;
            if(curr_input>=MAX_INPUTS){return false;}
        }
    }

    for (uint8_t vce=0;vce<MAX_VOICES;vce++)
    {
        for(uint8_t ins=0;ins<MAX_INPUTS_PER_OBJ;ins++)
        {
            voices[vce].voice_in_table_id[ins]=curr_input;
            if(ins<VOICES_INPUTS_NB){
                char buf[IN_OUT_NAME_LEN]={'V','C','E','S'};
                convIntToString(buf+4,(uint32_t)vce,2);
                memcpy(buf+6,&voices_inputs_names[ins],OBJ_IO_NAME_LEN-1);
                memcpy(in_table_name[curr_input],buf,IN_OUT_NAME_LEN);
            }
            curr_input++;
            if(curr_input>=MAX_INPUTS){return false;}
        }
    }

    // ajouter ici d'autres entrées  (sequencers etc)

    return true;
}  

void objects_table_init()
{
    init_objects_inputs();
    /*for (int16_t k=0;k<MAX_INPUTS;k++){
        if(in_table_name[k][0]!=0){
            printf("%i n:%s\n",k,in_table_name[k]);
        }
    }*/
    init_objects_outputs();
}

static spin_lock_t *inputs_id__lock;

void connect_input(uint16_t id, uint16_t output, uint8_t type_norm)
{
    uint32_t f = spin_lock_blocking(inputs_id__lock);

    // Initialisation du maillon
    in_table_type_norm[id]     = type_norm;
    in_table_id[id]            = NO_LINK;

    int16_t next_id = *out_table_val[output];

    if(next_id==NO_LINK){*out_table_val[output]=id;}

    else {
        int16_t prev=next_id;

        while (next_id != NO_LINK) {                  // fin de chaine ? 
            prev=next_id;
            next_id=in_table_id[next_id];
        }        
        in_table_id[prev]=id;
    }        

    spin_unlock(inputs_id__lock, f);
}

void disconnect_input(uint16_t id, uint16_t output)
{
    uint32_t f = spin_lock_blocking(inputs_id__lock);

    int16_t first = *out_table_val[output];

    // chaîne vide → rien à faire
    if (first == NO_LINK) {
        spin_unlock(inputs_id__lock, f);
        return;
    }

    // cas 1 : le maillon à retirer est en tête
    if (first == id) {
        *out_table_val[output] = in_table_id[id];   // nouveau head = suivant
        in_table_id[id] = NO_LINK;            // nettoie le maillon
        spin_unlock(inputs_id__lock, f);
        return;
    }

    // cas 2 : maillon au milieu / fin
    int16_t prev = first;
    int16_t curr = in_table_id[first];

    while (curr != NO_LINK && curr != id) {
        prev = curr;
        curr = in_table_id[curr];
    }

    if (curr == id) {
        // on saute le maillon courant
        in_table_id[prev] = in_table_id[curr];
        in_table_id[curr] = NO_LINK;         // nettoie le maillon
    }

    spin_unlock(inputs_id__lock, f);
}

void update_inputs(uint16_t output,int16_t valeur)
{
    int16_t id = *out_table_val[output];

    if (__builtin_expect(id != NO_LINK, 0))
    {
        do {
            int16_t next = in_table_id[id];
            in_table_val[id] = valeur;
            // norm_valeur(in_table_val[id],in_table_norm[id])
            id = next;
        } while (id != NO_LINK);
    }
}