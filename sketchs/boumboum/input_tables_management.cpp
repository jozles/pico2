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

extern Voice voices[MAX_VOICES];

int16_t in_table_val[MAX_INPUTS];
int16_t in_table_id[MAX_INPUTS];
int16_t in_table_type_norm[MAX_INPUTS];
uint8_t type_norm[MAX_INPUTS];

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

// ajouter ici d'autres générateurs lents  (sequencers, kbd etc) ps: les voices n'ont pas de sorties lentes

    for (uint16_t i=0;i<MAX_OUTPUTS;i++){if(outputs[i]!=nullptr){*outputs[i]=NO_LINK;}}

    return true;
}

bool init_objects_input_ptr(void)
{
    for (uint16_t i=0;i<MAX_INPUTS;i++){
        in_table_id[i]=NO_LINK;
    }

    int16_t curr_input=0;

    for (uint8_t lfo=0;lfo<MAX_LFO;lfo++)
    {
        for(uint8_t ins=0;ins<MAX_INPUTS_PER_OBJ;ins++)
        {
            lfo_in_table_id[lfo][ins]=curr_input;
            curr_input++;
            if(curr_input>=MAX_INPUTS){return false;}
        }
    }

    for (uint8_t adsr=0;adsr<MAX_ADSR;adsr++)
    {
        for(uint8_t ins=0;ins<MAX_INPUTS_PER_OBJ;ins++)
        {
            adsr_in_table_id[adsr][ins]=curr_input;
            curr_input++;
            if(curr_input>=MAX_INPUTS){return false;}
        }
    }

    for (uint8_t vce=0;vce<MAX_VOICES;vce++)
    {
        for(uint8_t ins=0;ins<MAX_INPUTS_PER_OBJ;ins++)
        {
            voices[vce].voice_in_table_id[ins]=curr_input;
            curr_input++;
            if(curr_input>=MAX_INPUTS){return false;}
        }
    }

    // ajouter ici d'autres entrées  (sequencers etc)

    return true;
}    

static spin_lock_t *inputs_id__lock;
void connect_input(uint16_t id, uint16_t output, uint8_t type_norm)
{
    uint32_t f = spin_lock_blocking(inputs_id__lock);

    // Initialisation du maillon
    in_table_type_norm[id]     = type_norm;
    in_table_id[id]            = NO_LINK;

    int16_t next_id = *outputs[output];

    if(next_id==NO_LINK){*outputs[output]=id;}

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

    int16_t first = *outputs[output];

    // chaîne vide → rien à faire
    if (first == NO_LINK) {
        spin_unlock(inputs_id__lock, f);
        return;
    }

    // cas 1 : le maillon à retirer est en tête
    if (first == id) {
        *outputs[output] = in_table_id[id];   // nouveau head = suivant
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
    int16_t id = *outputs[output];

    if (__builtin_expect(id != NO_LINK, 0))
    {
        do {
            int16_t next = in_table_id[id];
            in_table_val[id] = valeur;
            id = next;
        } while (id != NO_LINK);
    }

    /*while (id != NO_LINK) {
        int16_t next = in_table_id[id]; // le préchargement gagne 1 cycle
        in_table_val[id]=valeur;
        // norm_valeur(in_table_val[id],in_table_norm[id])
        id=next;
    }*/
}