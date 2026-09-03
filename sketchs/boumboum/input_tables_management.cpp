#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "const.h"
#include "frequences.h"
#include "miscControls.h"
#include "input_tables_management.h"
#include "sound_level_management.h"
#include "util.h"
#include "std_utils.h"

#include "hardware/sync.h"

extern int16_t   objects_first_output_id[];
extern int16_t   objects_first_input_id[];

extern uint16_t  adsrCoderAtt[MAX_ADSR];
extern uint16_t  adsrCoderDec[MAX_ADSR];
extern uint16_t  adsrCoderSus[MAX_ADSR];
extern uint16_t  adsrCoderRel[MAX_ADSR];
extern uint16_t  adsrCoderLev[MAX_ADSR];
extern uint16_t  adsrCoderAttAtt[MAX_ADSR];
extern uint16_t  adsrCoderDecAtt[MAX_ADSR];
extern uint16_t  adsrCoderSusAtt[MAX_ADSR];
extern uint16_t  adsrCoderRelAtt[MAX_ADSR];
extern uint16_t  adsrCoderLevAtt[MAX_ADSR];
extern uint8_t   adsrStatus[MAX_ADSR];
extern uint32_t  adsrCurrEch[MAX_ADSR];
extern uint32_t  adsrCurrEchFra[MAX_ADSR];
extern int16_t   adsr_ctl_input_id[][MAX_INPUTS_PER_OBJ];
extern int16_t   adsr_ctl_output_id[][MAX_OUTPUTS_PER_OBJ];
//extern uint16_t  adsrOutputsValues[MAX_ADSR][MAX_OUTPUTS_PER_OBJ];

extern int16_t   tbut_ctl_output_id[][MAX_OUTPUTS_PER_OBJ];

extern Voice voices[MAX_VOICES];

extern float     lfosFrequency[MAX_LFO]; 
extern uint16_t  lfosCoderCycleR[MAX_LFO];
extern uint16_t  lfosCycleR[MAX_LFO];
extern uint16_t  lfosCoderCycleRAtt[MAX_LFO];
extern uint16_t  lfosCodersFreq[MAX_LFO];
extern uint16_t  lfosCodersFreqAtt[MAX_LFO];
extern int16_t   lfo_ctl_input_id[][MAX_INPUTS_PER_OBJ];
extern int16_t   lfo_ctl_output_id[][MAX_OUTPUTS_PER_OBJ];
extern int16_t   lfosOutputsValues[MAX_LFO][MAX_OUTPUTS_PER_OBJ]; 

extern uint16_t  lfmCoder[MAX_LFM_INPUTS][MAX_LFM];
extern uint16_t  lfmCoderAtt[MAX_LFM_INPUTS][MAX_LFM];
extern int16_t   lfm_ctl_input_id[MAX_INPUTS_PER_OBJ][MAX_LFM];
extern int16_t   lfm_ctl_output_id[MAX_OUTPUTS_PER_OBJ][MAX_LFM];
extern int16_t   intermediateOutputValues[MAX_LFM];
extern int16_t   lfmOutputValues[MAX_LFM];
extern uint8_t   lfmNb[MAX_INPUTS];
 

/* ************            objets           ************ */

// les objets de l'application sont des boites munies d'entrées et de sorties
//
// il y a 2 types d'entrées : 
//      les controles (à chacun est associé un coder d'atténuation) 
//      les signaux audio 
// chaque entrée a un codeur de valeur manuelle + coder d'atténuation pour une valeur "externe"
// un paramètre interne de "normalisation" associé à chaque entrée sert à leur mise à l'échelle
// la valeur finale de l'entrée est le résultat de la fonction setxxxxyyyy (ci-aprés) 
// le nombre maxi d'entrées est fixe pour tous les objets et en général excédentaire
// Donc, pour chaque entrée d'objet, il y a 4 variables stockées : valeur du coder de niveau manuel, niveau manuel normalisé, valeur de normalisation et valeur de coder d'atténuation
// En cours de développement, les valeurs de normalisation ne sont pas toujours implémentées
//
// la mise à jour d'une entrée d'un objet se fait avec la fonction set/objet/paramètre (ex setAdsrDur ou setLFoFreq)
// avec des arguments selon le type d'objet (ex le n° d'objet, l'entrée concernée et la valeur de l'entrée - setAdsrDur(adsr,ADSR_ATT,dur) )
// Cas des fréquences et durées
//      En principe le coder et l'entrée sont linéaires et la conversion de la somme est à la fin de la fonction setxxxxyyyy 
//      cette somme est en principe un step dans une table, la valeur du step étant la vitesse de lecture de la table
//      la table est lue dans le handler de l'objet (lfosHandler,AdsrHandler,FillVoices etc) à une fréquence d'échantillonage propre.
// Cas des amplitudes
//      Il s'agit de sons qui sont à ajouter : les 2 niveaux (coder et entrée atténuée/normalisée) sont convertis puis la somme est effecuée
//      cette somme est l'indice d'une table de dé-linéarisation (amplLevel[])
// la fonction set est utilisée 3 fois : dans les inits, dans le menu de saisie des coders et dans la mise à jour des entrées (update_inputs)
// ainsi les paramètres utilisés dans la production des voices sont tenus à jour en temps réel (ils sont échantillonnés à chaque début de remplissage de buffer de son)
//
// il y a 2 types de sorties :
//      les signaux audio
// les signaux audios sont sommés voix par voix par la fonction fillvoice ; chaque onde et bruit a une entrée d'amplification à 16 bits
//      les contrôles
// les contrôles sont des valeurs 16 bits signés (coders, sorties des lfos, adsr, switchs etc)
// ils sont recadrés selon le type de l'entrée à laquelle ils sont appliqués (par ex, une fréquence est sur 13 bits, un rc sur 5 bits, une durée sur 7 bits, les binaires (touchb) 0/1)
// comme déjà dit, coders et sorties atténuées sont "ajoutés" selon le type d'entrée
// le nombre de sorties possibles est fixe pour tous les objets et le plus souvent excédentaire
// 
// les variables décrivant les objets sont réparties entre
//      les objets (jeu de tables indicées sur le numéro d'objet)
//      les entrées de controle (jeu de tables indicées sur le numéro d'entrée)
//      les sorties de controle (jeu de tables indicées sur le numéro de sortie)
// le "cablage" entre entrées et sorties se fait au moyen de ces tables (voir description ci-après)
// 
// la production des valeurs de sortie de chaque objet est cadencée via plusieurs horloges (irq)
// la fabrication des valeurs est incorporée au producteur pour les basses fréquences
// 
// objets :
//      voices  =   oscillateurs à fréquences audio fournissant sinus, triangle, dent de scie, carré
//                  l'ensemble à rapport cyclique réglable, bruit blanc et rose. Les 6 signaux mixés
//      lfos    =   oscillateurs à fréquences audio fournissant sinus, triangle, dent de scie, carré
//                  l'ensemble à rapport cyclique réglable
//      touch b =   touches du pupitre de commande
//      shapers =   séquences à 4 étapes (adsr) + niveau de sustain ; déclenchement selon trig et tlev 
//      mixers  =   mélangeurs audio ou de controles (atténuateurs pour chaque entrée ; ampli de sortie pour les audio)
//      séquenceurs = générateurs d'impulsions et/ou valeurs 16bits programmables
//      générateurs d'écho = délai, niveau
//      générateurs de réverbération, durée, niveau
//
// Ajouter un objet nécessite plusieurs interventions :
//      créer sa description (structure comme voice ou tables comme lfo)
//      l'ajouter à la liste dans objects.def
//      créer les 2 fichiers *.def pour décrire ses entrées et sorties
//      ajouter pour chaque entrée un nom de type dans norm_types.def
//      ajouter ses constantes dans const.h (MAX_xxx à utiliser dans la ligne MAX_OUTPUTS)
//      ajouter un paragraphe enum d'entrées et de sorties dans const.h
//      ajouter un paragraphe nom des entrées et des sorties ci-après
//      ajouter un paragraphe d'init dans init_objects_inputs et init_objects_outputs ci-après
//      ajouter la fonction setxxxxyyyy vue plus haut
//      ajouter le traitement d'update dans update_inputs
//      ajouter un menu (ligne d'appel dans boumboum, inits dans boumboum et menu, traitement de ligne dans menu)
//      ajouter un handler à l'endroit approprié

/* ************ control inputs and outputs ************* */

// each control input of each object has an unique id wich gives access to its parameters (value, source, norm, name etc)
// each object type has an [object_type]_ctl_input_id table with [object_type#][input#] elements 
// (the ids table for the inputs of every objects of this type)
// objects inputs are described in files [object_type]_input_names.def (ex: lfos_inputs_names.def)
// ex: lfo#3 input#1 has the id : lfo_ctl_input_id[3][1] wich is the index in the tables ctl_input_xxx[]
// or: lfo_ctl_input_id[3][LCRA]
// table ctl_input_id_chain[] allows to chain the inputs wich are connected to the same output for faster access

int16_t ctl_input_val[MAX_INPUTS];                       // all inputs values
int16_t ctl_input_prev_val[MAX_INPUTS];                  // all inputs prev values for trig level identification
int16_t ctl_input_srce[MAX_INPUTS];                      // all inputs sources ie outputs used for inputs (usefull for disconnection) 
//uint8_t ctl_input_norm[MAX_INPUTS];                      // all inputs norm type (0 nothing ; 1 lfo_freq ; 2 vce freq ; 3 rc ; 4 ampl 0-31 etc)
int16_t ctl_input_shft[MAX_INPUTS];                      // all inputs values shift type (0 no shift ; in)  ??????? input shifting no yet implemented
uint8_t ctl_input_trig[MAX_INPUTS];                      // all inputs trig type ; reset when read
int16_t ctl_input_tlev[MAX_INPUTS];                      // all inputs trig level
char    ctl_input_name[MAX_INPUTS][IN_OUT_NAME_LEN];     // all inputs names
uint8_t ctl_input_update_type[MAX_INPUTS];               // all inputs update specific job
uint8_t ctl_input_object[MAX_INPUTS];                    // all inputs object#

int16_t ctl_input_id_chain[MAX_INPUTS];                  // next id(input) with same output (-1/NO_LINK if nothing)

uint8_t norm_dividers[]={0,16-5,16-6,16-3};              // to add ; not used for now

// each control output of each object has an unique id wich gives access to the name and input link chain of the output
// each object type has an [object]_ctl_output_id table with [object_type#][output#] elements
// (ids table for the outputs of every objects of this type)
// objects outputs are described in files [object_type]_output_names.def (ex: lfos_outputs_names.def)
// ex: lfo#2 output#3 has the id : lfo_ctl_output_id[2][3] wich is the index in the tables ctl_output_xxx[] 
// or: lfo_ctl_output_id[2][LSAW]
// table ctl_output_id_chain[] (one element per output) allows to chain the inputs wich are connected to this output for faster access
// contains first input id of the chain or -1/NO_LINK

/* ************** fichiers *.def des enum *************** */

// les objects sont listés dans le fichier objects.def
// chaque objet a une liste de ses entrées (0-n) avec un mnémo associé pour l'utilitaire de cablage [objet]_inputs_names.def
// pareil pour ses sorties [objet]_outputs_names.def
// une autre liste concerne les procédures de mise à jour des valeurs des entrées
//

//int16_t*  ctl_output_val[MAX_OUTPUTS];                    // all objects ptrs to outputs values
char      ctl_output_name[MAX_OUTPUTS][IN_OUT_NAME_LEN];    // all objects outputs names
int16_t   ctl_output_id_chain[MAX_OUTPUTS];    // all objects outputs chain to input (first link)

// ***** noms des entrées/sorties des objets *****

const char lfo_inputs_names[][OBJ_IO_NAME_LEN]={         
    #define X(name,text) text,
    #include "lfos_inputs_names.def"   
    #undef X   
};

const char voices_inputs_names[][OBJ_IO_NAME_LEN]={
    #define X(name,text) text,
    #include "vces_inputs_names.def"   
    #undef X   
};

const char adsr_inputs_names[][OBJ_IO_NAME_LEN]={
    #define X(name,text) text,
    #include "adsr_inputs_names.def"   
    #undef X   
};

const char lfm_inputs_names[][OBJ_IO_NAME_LEN]={
    #define X(name,text) text,
    #include "lfm_inputs_names.def"   
    #undef X   
};

const char lfo_outputs_names[][OBJ_IO_NAME_LEN]={
    #define X(name,text) text,
    #include "lfos_outputs_names.def"   
    #undef X   
};

const char vces_outputs_names[][OBJ_IO_NAME_LEN]={
    #define X(name,text) text,
    #include "vces_outputs_names.def"   
    #undef X   
};

const char adsr_outputs_names[][OBJ_IO_NAME_LEN]={
    #define X(name,text) text,
    #include "adsr_outputs_names.def"   
    #undef X   
};

const char tbut_outputs_names[][OBJ_IO_NAME_LEN]={
    #define X(name,text) text,
    #include "tbut_outputs_names.def"   
    #undef X   
};

const char lfm_outputs_names[][OBJ_IO_NAME_LEN]={
    #define X(name,text) text,
    #include "lfm_outputs_names.def"   
    #undef X   
};


bool init_objects_outputs(void)
{
    memset(ctl_output_name,'-',MAX_OUTPUTS*IN_OUT_NAME_LEN);
    int16_t curr_output=1;            // output 0 is null

    objects_first_output_id[LFO______]=curr_output;
    for (uint8_t lfo=0;lfo<MAX_LFO;lfo++)
    {
        for(uint8_t outs=0;outs<MAX_OUTPUTS_PER_OBJ;outs++)
        {
            ctl_output_id_chain[curr_output]=NO_LINK;
            lfo_ctl_output_id[lfo][outs]=curr_output;
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

    objects_first_output_id[ADSR_____]=curr_output;    
    for (uint8_t adsr=0;adsr<MAX_ADSR;adsr++)
    {
        for(uint8_t outs=0;outs<MAX_OUTPUTS_PER_OBJ;outs++)
        {        
            ctl_output_id_chain[curr_output]=NO_LINK;
            adsr_ctl_output_id[adsr][outs]=curr_output;
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

    objects_first_output_id[TBUTTON__]=curr_output;    
    for (uint8_t tbut=0;tbut<MAX_TBUT;tbut++)
    {
        for(uint8_t outs=0;outs<MAX_OUTPUTS_PER_OBJ;outs++)
        {        
            ctl_output_id_chain[curr_output]=NO_LINK;
            tbut_ctl_output_id[tbut][outs]=curr_output;
            if(outs<TBUT_OUTPUTS_NB){
                char buf[IN_OUT_NAME_LEN]={'T','B','U','T'};
                convIntToString(buf+4,tbut,2);
                memcpy(buf+6,&tbut_outputs_names[outs],OBJ_IO_NAME_LEN-1);
                memcpy(ctl_output_name[curr_output],buf,IN_OUT_NAME_LEN);
            }       
            curr_output++;
            if(curr_output>=MAX_OUTPUTS){return false;}
        }        
    }

    objects_first_output_id[LF_MUX___]=curr_output;    
    for (uint8_t lfm=0;lfm<MAX_LFM;lfm++)
    {
        for(uint8_t outs=0;outs<MAX_OUTPUTS_PER_OBJ;outs++)
        {        
            ctl_output_id_chain[curr_output]=NO_LINK;
            lfm_ctl_output_id[lfm][outs]=curr_output;
            if(outs<LFM_OUTPUTS_NB){
                char buf[IN_OUT_NAME_LEN]={'L','F','M','_'};
                convIntToString(buf+4,lfm,2);
                memcpy(buf+6,&lfm_outputs_names[outs],OBJ_IO_NAME_LEN-1);
                memcpy(ctl_output_name[curr_output],buf,IN_OUT_NAME_LEN);
            }
            curr_output++;
            if(curr_output>=MAX_OUTPUTS){return false;}
        }        
    }

// ajouter ici d'autres générateurs lents  (sequencers, kbd etc) ps: les voices n'ont pas de sorties lentes

    printf("init_obects_outputs(%u)\n",curr_output-1);

    return true;
}

bool init_objects_inputs(void)
{

    uint8_t inputNum=3;
    uint8_t objName=3;
    uint8_t objNum=2;
    uint8_t objOutType=3;
    uint8_t lenInpName=objName+objNum+objOutType;

    memset(ctl_input_name,0x00,MAX_INPUTS*IN_OUT_NAME_LEN);
    memset(ctl_input_srce,0x00,MAX_INPUTS);
    memset(ctl_input_shft,0x00,MAX_INPUTS);
    memset(ctl_input_trig,0x00,MAX_INPUTS);
    memset(ctl_input_tlev,0x00,MAX_INPUTS);
    
    for (uint16_t i=0;i<MAX_INPUTS;i++){
        ctl_input_id_chain[i]=NO_LINK;
        ctl_input_val[i]=0;
    }

    memset(ctl_input_name[0],'-',objName);
    int16_t curr_input=1;

    objects_first_input_id[LFO______]=curr_input;
    for (uint8_t lfo=0;lfo<MAX_LFO;lfo++)
    {
        for(uint8_t ins=0;ins<MAX_INPUTS_PER_OBJ;ins++)
        {
            lfo_ctl_input_id[lfo][ins]=curr_input;
            ctl_input_id_chain[curr_input]=NO_LINK;
            ctl_input_object[curr_input]=lfo;
            switch(ins){
                case LFRQ:ctl_input_update_type[curr_input]=LFO_FREQ;break;
                case LCR_:ctl_input_update_type[curr_input]=LFO_CRA;break;
            }
            
            if(ins<LFO_INPUTS_NB){
                char buf[IN_OUT_NAME_LEN]={'L','F','O','S'};
                convIntToString(buf+objName,lfo,objNum);
                memcpy(buf+objName+objNum,&lfo_inputs_names[ins],objOutType);  //OBJ_IO_NAME_LEN-1);
                memcpy(ctl_input_name[curr_input],buf,lenInpName);   //IN_OUT_NAME_LEN);
            }
            curr_input++;
            if(curr_input>=MAX_INPUTS){return false;}
        }
    }
    //printf(" lfo_curr_input  :%u %u\n",objects_first_input_id[LFO______],curr_input-1);

    objects_first_input_id[ADSR_____]=curr_input;
    for (uint8_t adsr=0;adsr<MAX_ADSR;adsr++)
    {
        for(uint8_t ins=0;ins<MAX_INPUTS_PER_OBJ;ins++)
        {
            adsr_ctl_input_id[adsr][ins]=curr_input;
            ctl_input_id_chain[curr_input]=NO_LINK;
            ctl_input_object[curr_input]=adsr;
            switch(ins){
                case ATTK:ctl_input_update_type[curr_input]=A_ATTACK;break;
                case DECA:ctl_input_update_type[curr_input]=A_DECAY ;break;
                case SUST:ctl_input_update_type[curr_input]=A_SUST  ;break;
                case RELE:ctl_input_update_type[curr_input]=A_RELEAS;break;
                case LEVE:ctl_input_update_type[curr_input]=A_LEVEL ;break;
                case STAR:ctl_input_update_type[curr_input]=A_START ;
                          ctl_input_trig[curr_input]=INP_UP_TRIG;
                          ctl_input_tlev[curr_input]=0;
                          //printf("id:%u adsr_input_trig:%u:%u t:%u l:%u \n",curr_input,adsr,ins,ctl_input_trig[curr_input],ctl_input_tlev[curr_input]);
                          break;
                default: break;
            }           
            if(ins<ADSR_INPUTS_NB){
                char buf[IN_OUT_NAME_LEN]={'A','D','S','R'};
                convIntToString(buf+objName,adsr,objNum);
                memcpy(buf+objName+objNum,&adsr_inputs_names[ins],objOutType);    //OBJ_IO_NAME_LEN-1);
                memcpy(ctl_input_name[curr_input],buf,lenInpName);   //IN_OUT_NAME_LEN);
            }
            curr_input++;
            if(curr_input>=MAX_INPUTS){return false;}
        }
    }
    //printf(" adsr_curr_input :%u %u\n",objects_first_input_id[ADSR_____],curr_input-1);

    objects_first_input_id[VOICE____]=curr_input;
    for (uint8_t vce=0;vce<MAX_VOICES;vce++)
    {
        for(uint8_t ins=0;ins<MAX_INPUTS_PER_OBJ;ins++)
        {
            voices[vce].voice_ctl_input_id[ins]=curr_input;
            ctl_input_id_chain[curr_input]=NO_LINK;
            ctl_input_object[curr_input]=vce;
            switch(ins){
                case VFRQ:ctl_input_update_type[curr_input]=VCE_FREQ;break;
                case VCR_:ctl_input_update_type[curr_input]=VCE_CRA;break;
                case VSPW:ctl_input_update_type[curr_input]=VCE_WSIN;break;
                case VTPW:ctl_input_update_type[curr_input]=VCE_WTRI;break;
                case VAPW:ctl_input_update_type[curr_input]=VCE_WSAW;break;
                case VQPW:ctl_input_update_type[curr_input]=VCE_WSQR;break;
                case VWPW:ctl_input_update_type[curr_input]=VCE_WHIT;break;
                case VKPW:ctl_input_update_type[curr_input]=VCE_WPNK;break;
                case VGPW:ctl_input_update_type[curr_input]=VCE_GENA;break;
                default:break;
            }
            if(ins<VOICES_INPUTS_NB){
                char buf[IN_OUT_NAME_LEN]={'V','C','E','S'};
                convIntToString(buf+objName,(uint32_t)vce,objNum);
                memcpy(buf+objName+objNum,&voices_inputs_names[ins],objOutType);  //OBJ_IO_NAME_LEN-1);
                memcpy(ctl_input_name[curr_input],buf,lenInpName);   //IN_OUT_NAME_LEN);
            }
            curr_input++;
            if(curr_input>=MAX_INPUTS){return false;}
        }
    }
    //printf(" voice_curr_input:%u %u\n",objects_first_input_id[VOICE____],curr_input-1);

    objects_first_input_id[LF_MUX___]=curr_input;
    for (uint8_t lfm=0;lfm<MAX_LFM;lfm++)
    {
        for(uint8_t ins=0;ins<MAX_INPUTS_PER_OBJ;ins++)
        {
            lfm_ctl_input_id[lfm][ins]=curr_input;
            ctl_input_id_chain[curr_input]=NO_LINK;
            ctl_input_object[curr_input]=lfm;
            ctl_input_update_type[curr_input]=LFM____;
            ctl_input_val[curr_input]=0;
            
            if(ins<MAX_LFM_INPUTS){
                char buf[IN_OUT_NAME_LEN]={'L','F','M','_'};
                convIntToString(buf+objName,lfm,objNum);
                *(buf+objName+objNum)='_';
                convIntToString(buf+objName+objNum+1,ins,2);
                //memcpy(buf+objName+objNum,&lfm_inputs_names[ins],objOutType); //OBJ_IO_NAME_LEN-1);
                memcpy(ctl_input_name[curr_input],buf,lenInpName);   //IN_OUT_NAME_LEN);
            }
            lfmNb[curr_input]=lfm;
            curr_input++;
            //printf("%u %s\n",curr_input-1,ctl_input_name[curr_input-1]);
            if(curr_input>=MAX_INPUTS){return false;}
        }
    }
    //printf(" lfm_curr_input  :%u %u\n",objects_first_input_id[LF_MUX___],curr_input-1);        

    // ajouter ici d'autres entrées  (sequencers etc)

    printf("init_obects_inputs(%u)\n",curr_input-1);

    return true;
}  

static spin_lock_t *inputs_id__lock;

void objects_table_init()
{
    inputs_id__lock = spin_lock_init(CTL_INPUTS_LOCK);

    if(!init_objects_inputs()){system_error("init_objects_inputs");}
    if(!init_objects_outputs()){system_error("init_objects_outputs");}
}

void __not_in_flash_func(connect_input)(uint16_t input_id, uint16_t output)     // add input_id at end of output_id_chain * setup ctl_input_srce[input_id] to output_id
{
    uint32_t f = spin_lock_blocking(inputs_id__lock);

    // link init only (norm/shft/trig/tlev update by menu_mapping)
    ctl_input_id_chain[input_id] = NO_LINK;

    int16_t next_id = ctl_output_id_chain[output];
    printf("\nctl_output_id_chain[%u]=%i ",output,ctl_output_id_chain[output]);

    if(next_id==NO_LINK)
        {ctl_output_id_chain[output]=input_id;
        ctl_input_srce[input_id]=output;
        printf("->%u\n",input_id);
        } // end of update

    else {
        int16_t prev=next_id;                   // first id of the chain from ctl_output_id_chain

        while (next_id != NO_LINK) {            // end of chain ? 
            prev=next_id;
            next_id=ctl_input_id_chain[prev];
            printf(" ciic[%i]=%i ",prev,next_id);
            if(next_id>MAX_INPUTS || next_id<NO_LINK){
                spin_unlock(inputs_id__lock, f);
                system_error("input_id overflow c",next_id);}
        }        
        ctl_input_id_chain[prev]=input_id;
        ctl_input_srce[input_id]=output;
        printf(" ciic[%i]->%i \n",prev,ctl_input_id_chain[prev]);
    }        

    spin_unlock(inputs_id__lock, f);

    printf("out#:%d in_id:%d out_id_chain:%i inp_id_chain:%i\n",output,input_id,ctl_output_id_chain[output],ctl_input_id_chain[input_id]);
}

void __not_in_flash_func(disconnect_input)(uint16_t input_id, uint16_t output)
{
    if(output==0){return;}

    uint32_t f = spin_lock_blocking(inputs_id__lock);
    int16_t first = ctl_output_id_chain[output];            // que faire quand output = 0 ??????????????????????????????????????????????????????????????

    // chaîne vide → rien à faire
    if (first == NO_LINK) {
        ctl_input_id_chain[input_id] = NO_LINK;
        ctl_input_srce[input_id] = NO_LINK;
        spin_unlock(inputs_id__lock, f);
        return;
    }
    // cas 1 : le maillon à retirer est en tête
    if (first == input_id) {
        ctl_output_id_chain[output] = ctl_input_id_chain[input_id];   // nouveau head = suivant
        ctl_input_id_chain[input_id] = NO_LINK;
        ctl_input_srce[input_id] = NO_LINK;                  
        spin_unlock(inputs_id__lock, f);
        return;
    }

    // cas 2 : maillon au milieu / fin
    int16_t prev = first;
    int16_t curr = ctl_input_id_chain[first];
   
    if(prev>MAX_INPUTS || prev<NO_LINK || curr>MAX_INPUTS || curr<NO_LINK){
        spin_unlock(inputs_id__lock, f);
        system_error("input_id overflow d1");}

    while (curr != NO_LINK && curr != input_id) {
        prev = curr;
        curr = ctl_input_id_chain[curr];
        
        if(curr>MAX_INPUTS || curr<NO_LINK){
            spin_unlock(inputs_id__lock, f);
            system_error("input_id overflow d2");}
    }

    if (curr == input_id) {
        // on saute le maillon courant
        ctl_input_id_chain[prev] = ctl_input_id_chain[curr];
        ctl_input_id_chain[curr] = NO_LINK;
        ctl_input_srce[curr] = NO_LINK;  
    }

    spin_unlock(inputs_id__lock, f);
}

void __not_in_flash_func(update_inputs)(int16_t id,int16_t valeur)  // inputs update with valeur
                                                                    // valeur est la valeur linéaire à atténuer sur 16 bits recadrée selon le type d'entrée
                                                                    // id numéro unique de l'input donne accès à toutes ses caractéristiques
                                                                    //  le ptr dans ctl_input_id_chain de l'id suivant recevant la meme output/valeur
                                                                    //  la valeur du coder associé
                                                                    //  le type d'entrée via ctl_input_update_type
                                                                    //  la valeur précédente de l'entrée via ctl_input_val
{
    //int16_t id = ctl_output_id_chain[output];
    int16_t prev = ctl_input_val[id];
    int16_t tlev;
    ctl_input_val[id] = valeur;
    uint8_t object=ctl_input_object[id];
    int32_t val;
    float fr;
    //uint8_t src0=src/MAX_OBJECTS;
    //uint8_t src_id=src-src0*MAX_OBJECTS;

    //if(output==10){printf("out#:%d input_id:%i inp_type:%d val:%i \n",output,id,ctl_input_update_type[id],valeur);}

        while(id!=NO_LINK){

            //printf("s:%u/%u u_i:%i=%s:%u v:%i ",src0,src_id,id,ctl_input_name+id*IN_OUT_NAME_LEN,ctl_input_update_type[id],valeur);

            switch(ctl_input_update_type[id]){
                case VCE_FREQ:  val=(valeur>>6)*voices[object].coderFreqAtt>>MAX_CTL_ATT_SHIFT;                // 8k max VCES_MAX_FREQ_CODERS ; att 0-255
                                fr=calcFreq(val+voices[object].coderFreq);
                                setVoiceFrequency(fr,&voices[object],voices[object].coderCycleR);       // ajouter un ctl d'overflow
                                break;
                case VCE_CRA :  val=voices[object].coderCycleR+(valeur>>10)*voices[object].coderCycleRAtt>>MAX_CTL_ATT_SHIFT; // 0-62 ; att 0-255                
                                setVoiceFrequency(voices[object].frequency,&voices[object],val);        // ajouter un ctl d'overflow
                                break;
                case VCE_WSIN:  setVoicesAmpl(object,WSIN,valeur);break;
                case VCE_WTRI:  setVoicesAmpl(object,WTRI,valeur);break;
                case VCE_WSAW:  setVoicesAmpl(object,WSAW,valeur);break;
                case VCE_WSQR:  setVoicesAmpl(object,WSQR,valeur);break;
                case VCE_WHIT:  setVoicesAmpl(object,WHIT,valeur);break;
                case VCE_WPNK:  setVoicesAmpl(object,PONK,valeur);break;                                                                                                                
                case VCE_GENA:  setVoicesAmpl(object,BASIC_WAVES_NB,valeur);break;
                case LFO_FREQ:  val=(valeur>>3)*lfosCodersFreqAtt[object]>>MAX_CTL_ATT_SHIFT;                  // 8k max VCES_MAX_FREQ_CODERS ; att 0-255 
                                fr=calcFreq(val+lfosCodersFreq[object])/VOICE_FREQ_DIVIDER;
                                //if(lfo==0){printf("l%d id:%d v:%i val:%i out#:%d fc:%i f:%f\n",lfo,id,valeur,val,output,lfosCodersFreq[lfo],fr);}
                                setLfosFrequency(fr,object,lfosCoderCycleR[object]);                    // ajouter un ctl d'overflow
                                break;
                case LFO_CRA :  val=lfosCoderCycleR[object]+(valeur>>10)*lfosCoderCycleRAtt[object]>>MAX_CTL_ATT_SHIFT; // 0-62 ; att 0-255
                                setLfosFrequency(lfosFrequency[object],object,val);                     // ajouter un ctl d'overflow
                                break;
                case A_ATTACK:  val=(valeur>>9)*adsrCoderAttAtt[object]>>MAX_CTL_ATT_SHIFT;                    // durée 0-127
                                setAdsrDur(object,adsrStatus[object],val);
                                break;
                case A_DECAY :  val=(valeur>>9)*adsrCoderDecAtt[object]>>MAX_CTL_ATT_SHIFT;                    // durée 0-127
                                setAdsrDur(object,adsrStatus[object],val);
                                break;
                case A_SUST  :  val=(valeur>>3)*adsrCoderSusAtt[object]>>MAX_CTL_ATT_SHIFT;                    // durée 0-127
                                setAdsrDur(object,adsrStatus[object],val);
                                break;
                case A_RELEAS:  val=(valeur>>3)*adsrCoderRelAtt[object]>>MAX_CTL_ATT_SHIFT;                    // durée 0-127
                                setAdsrDur(object,adsrStatus[object],val);
                                break;
                case A_LEVEL :  val=(valeur>>3)*adsrCoderLevAtt[object]>>MAX_CTL_ATT_SHIFT;                    
                                setAdsrLev(object,val);
                                break;                        
                case A_START :  ctl_input_prev_val[id]=prev;                                            // start adsr
                                tlev=ctl_input_tlev[id];
                                //printf("t:%u l:%i p:%i",ctl_input_trig[id],tlev,prev);
                                switch(ctl_input_trig[id]){
                                    case INP_NO_TRIG:break;
                                    case INP_UP_TRIG:
                                        if(valeur>=tlev && prev<tlev){
                                            adsrStatus[object]=ADSR_ATT;
                                            adsrCurrEch[object]=0;
                                            //adsrCurrEchFra[object]=0;
                                        }
                                        break;
                                    case INP_DOWN_TRIG:
                                        if(valeur<=tlev && prev>tlev){
                                            adsrStatus[object]=ADSR_ATT;
                                            adsrCurrEch[object]=0;
                                            //adsrCurrEchFra[object]=0;
                                        }
                                        break;
                                    case INP_U_D_TRIG:
                                        if((valeur>tlev && prev<tlev) || (valeur<=tlev && prev>tlev)){
                                            adsrStatus[object]=ADSR_ATT;
                                            adsrCurrEch[object]=0;
                                            //adsrCurrEchFra[object]=0;
                                        }
                                        break;
                                    default:break;
                                }
                                //printf("s:%u ",adsrStatus[object]);
                                break;
                case LFM____ :  {
                                uint32_t carry_hi;
                                uint32_t carry_lo;
                                uint8_t  lfm=lfmNb[id];                                  // lfm #
                                uint8_t  inp=id-lfm_ctl_input_id[lfm][0];                // lfm inp #
                                int32_t  vv=(valeur*lfmCoderAtt[inp][lfm])>>MAX_CTL_ATT_SHIFT;
                                int16_t* iov=&intermediateOutputValues[lfm];                  
                                int32_t  iv=*iov;
                                // inp 0 global lin ; 1 global log ; 2,3 lin ; 4,5 log                                              
                                switch(inp){
                                    case 0:
                                        break;
                                    case 1:
                                        break;
                                    case 2:
                                        val=lfmCoder[inp][lfm]+vv;      // actual inp value toujours <0x00ffffff
                                        //carry=(val<=NO_ATTENUATION_VALUE);
                                        //ctl_input_val[id] = (val & -carry) | (NO_ATTENUATION_VALUE & ~(-carry));

                                        // iv, val : int32_t
                                        // iov : int16_t*
                                        // NO_ATTENUATION_VALUE = 0x7FFF
                                        // MAX_CTL_ATT_SHIFT = 8

                                        iv += val - *iov;   // intermediate value

                                        // ---- Saturation haute (+32767) ----
                                        carry_hi = (iv <= 0x7FFF);
                                        iv = (iv & -carry_hi) | (0x7FFF & ~(-carry_hi));

                                        // ---- Saturation basse (-32768) ----
                                        carry_lo = (iv >= -0x8000);
                                        iv = (iv & -carry_lo) | (-0x8000 & ~(-carry_lo));

                                        *iov = (int16_t)iv;   // safe cast (iv est maintenant dans [-32768..32767])

                                        // ---- Recalcul ----
                                        iv = *iov * (lfmCoder[0][lfm] + (lfmCoderAtt[0][lfm] >> MAX_CTL_ATT_SHIFT));

                                        // ---- Saturation haute (+32767) ----
                                        carry_hi = (iv <= 0x7FFF);
                                        iv = (iv & -carry_hi) | (0x7FFF & ~(-carry_hi));

                                        // ---- Saturation basse (-32768) ----
                                        carry_lo = (iv >= -0x8000);
                                        iv = (iv & -carry_lo) | (-0x8000 & ~(-carry_lo));

                                        lfmOutputValues[lfm] = (int16_t)iv;

                                                         
                                        update_inputs(ctl_output_id_chain[lfm_ctl_output_id[0][lfm]],lfmOutputValues[lfm]);    // ctl_output_id_chain[adsr_ctl_output_id[a][ADSR_SHAPE]];

                                        printf("m:%u-%u old:%i iv:%i v:%i ov:%i\n",lfm,inp,prev,valeur,val,lfmOutputValues[lfm]);
                                        break;
                                    case 3:
                                        break;
                                    default:break;
                                }                                    
                }break;
                default: break;
            }
            //printf("\n");
            id = ctl_input_id_chain[id];
        }
}
