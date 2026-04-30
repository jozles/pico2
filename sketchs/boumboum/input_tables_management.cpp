#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "const.h"
#include "frequences.h"
#include "miscControls.h"
#include "input_tables_management.h"

#include "hardware/sync.h"

extern int16_t   lfo_in_table_id[][MAX_OUTPUTS_PER_OBJ];
extern int16_t   adsr_in_table_id[][MAX_OUTPUTS_PER_OBJ];

static Input_lnk in_table[MAX_OBJ*MAX_INPUTS_PER_OBJ];

// pointeurs vers les premiers indices des sorties
static int16_t*  outputs[MAX_OUTPUTS];

bool init_objects_output_ptr(void)
{
    for (uint16_t i=0;i<MAX_OUTPUTS;i++){outputs[i]=nullptr;}

    int16_t curr_output=0;

    for (uint8_t lfo=0;lfo<MAX_LFO;lfo++)
    {
        for(uint8_t outs=0;outs<MAX_OUTPUTS_PER_OBJ;outs++)
        {
            outputs[curr_output]=&lfo_in_table_id[lfo][outs];
            curr_output++;
            if(curr_output>=MAX_OUTPUTS){return false;}
        }
    }

    for (uint8_t adsr=0;adsr<MAX_ADSR;adsr++)
    {
        for(uint8_t outs=0;outs<MAX_OUTPUTS_PER_OBJ;outs++)
        {
            outputs[curr_output]=&adsr_in_table_id[adsr][outs];
            curr_output++;
            if(curr_output>=MAX_OUTPUTS){return false;}
        }
    }

// ajouter ici d'autres générateurs lents  (sequencers, kbd etc)

    for (uint16_t i=0;i<MAX_OUTPUTS;i++){if(outputs[i]!=nullptr){*outputs[i]=NO_LINK;}}

    return true;
}

static spin_lock_t *inputs_id__lock;
void connect_input(Input_lnk* input, uint16_t output, uint8_t type_norm)
{
    uint32_t f = spin_lock_blocking(inputs_id__lock);

    // Initialisation du maillon
    input->type_norm     = type_norm;
    input->next_input_id = NO_LINK;

    // Récupération de la tête de chaîne (indice du premier maillon)
    int16_t  new_id  = input-&in_table[0];     // indice prochaine entrée
    int16_t* next_id = outputs[output];

    while (*next_id != NO_LINK) {                  // fin de chaine ? 
        next_id=&in_table[*next_id].next_input_id;
    }        

    *next_id = new_id;

    spin_unlock(inputs_id__lock, f);
}


void update_inputs(uint16_t output,int16_t valeur)
{
    int16_t id = *outputs[output];

    while (id != NO_LINK) {
        Input_lnk* p=&in_table[id];
        p->valeur=valeur;
        id=p->next_input_id;
    }
}
