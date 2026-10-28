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

extern uint8_t   adsrStatus[MAX_ADSR];
extern uint32_t  adsrCurrEch[MAX_ADSR];
extern int16_t   adsr_ctl_input_id[][ADSR_INPUTS_NB];
extern int16_t   adsr_ctl_output_id[][MAX_OUTPUTS_PER_OBJ];
//extern uint16_t  adsrOutputsValues[MAX_ADSR][MAX_OUTPUTS_PER_OBJ];

extern int16_t   tbut_ctl_output_id[][MAX_OUTPUTS_PER_OBJ];

extern Voice voices[MAX_VOICES];

extern float     lfosFrequency[MAX_LFO]; 
extern uint16_t  lfosCodersFreq[MAX_LFO];
extern uint16_t  lfosCodersFreqAtt[MAX_LFO];
extern int16_t   lfo_ctl_input_id[][LFO_INPUTS_NB];
extern int16_t   lfo_ctl_output_id[][MAX_OUTPUTS_PER_OBJ];
extern int16_t   lfosOutputsValues[MAX_LFO][MAX_OUTPUTS_PER_OBJ]; 

extern uint16_t  lfmCoder[LFM_INPUTS_NB][MAX_LFM];
extern uint16_t  lfmCoderAtt[LFM_INPUTS_NB][MAX_LFM];
extern int16_t   lfm_ctl_input_id[LFM_INPUTS_NB][MAX_LFM];
extern int16_t   lfm_ctl_output_id[MAX_OUTPUTS_PER_OBJ][MAX_LFM];
extern int16_t   intermediateOutputValues[MAX_LFM];
extern int16_t   lfmGenAttValue[MAX_LFM];
extern int16_t   lfmOutputValues[MAX_LFM];
extern uint8_t   lfmInputType[LFM_INPUTS_NB][MAX_LFM];

extern int16_t   rev_ctl_input_id[REVB_INPUTS_NB][MAX_REV];
extern int16_t   echo_ctl_input_id[ECHO_INPUTS_NB][MAX_ECHO];


/* ************            objets           ************ */

// les objets de l'application sont des boites munies d'entrées et de sorties
//
// il y a 2 types d'entrées : 
//      les signaux de controle
//      les signaux audio 
// chaque entrée a un codeur de valeur manuelle + 1 coder d'atténuation pour la valeur "externe" 
// certains paramètres d'objets n'ont pas d'entrée externe donc juste un coder (durées des adsr)
// un paramètre interne de "normalisation" associé à chaque entrée sert à leur mise à l'échelle (inutilisé?)
// le nombre maxi d'entrées est fixe pour tous les objets et en général excédentaire
// Donc, pour chaque entrée d'objet, il y a au moins : valeur du coder de base, valeur de coder d'atténuation, niveau manuel normalisé, valeur de normalisation
// En cours de développement, les valeurs de normalisation ne sont pas toujours implémentées
//
// Dynamique :
//      Lorsque quelque chose change, l'effet doit être reporté sur tout ce qui est concerné
//      Le changement d'un coder de base peut ne concerner que la caractéristique concernée (level des adsr), ou nécessiter un recalcul de l'entrée (freq de voice), ou nécessiter un recalcul global de la sortie (lfm)
//      Le changement d'un coder (base ou atténuation) ou d'une valeur d'entrée nécessite au moins le recalcul de l'entrée, 
//          et si c'est un objet de controle, la mise à jour de la sortie et de toutes les entrées chainées
//
// la mise à jour d'une entrée d'un objet se fait avec la fonction set/objet/paramètre (ex setAdsrDur, setLFoFreq, setLfm)
// avec des arguments selon le type d'objet (ex le n° d'objet, l'entrée concernée et la valeur de l'entrée - setAdsrDur(adsr,ADSR_ATT,dur) )
// la mise à jour des entrées chainées sur une sortie se fait avec la fonction update_inputs(id,valeur) qui remonte la chaine des id
// la fonction setxxxxyyyyCoders charge les coders dans les variables adhoc de l'objet puis exécute setxxxxyyyy qui met à jour les variables internes de l'objet et fait update_inputs si une/des sorties sont modifiées
// 
// Cas des fréquences et durées
//      En principe le coder et l'entrée sont linéaires et la conversion de la somme est à la fin de la fonction setxxxxyyyy 
//      cette somme est en principe un step dans une table, la valeur du step étant la vitesse de lecture de la table
//      la table est lue dans le handler de l'objet (lfosHandler,AdsrHandler,FillVoices etc) à une fréquence d'échantillonage propre.
// Cas des amplitudes
//      Il s'agit de sons qui sont à ajouter : les 2 niveaux (coder de base et entrée atténuée/normalisée) sont convertis puis la somme est effectuée
//      cette somme est l'indice d'une table de dé-linéarisation (amplLevel[])
// Cas banalisé (devrait devenir la norme) (ex lfm) : les coders sont des valeurs 8 bits ; 
//      le 'base coder' est stocké dans les params de l'objet en valeur 8 bits ; lors de son utilisation il est shifté en int16
//      le 'att coder' est stocké dans les params de l'objet en valeur 8 bits; il multiplie la valeur entrée et le résultat est >>8 (atténuateur)
//      la valeur entrée provient d'une sortie d'objet en int16_t 
//      la mise à jour des coders se fait via la fonction set qui recalcule éventuellemnt la sortie via update_inputs 
// la fonction set est utilisée 3 fois : dans les inits, dans le menu de saisie des coders (menuLineDsp) et dans la mise à jour des entrées (update_inputs)
// ainsi les paramètres utilisés dans la production des voices sont tenus à jour en temps réel (ils sont échantillonnés à chaque début de remplissage de buffer de son)
//
// il y a 2 types de sorties :
//      les signaux audio
//          les signaux audios sont sommés voix par voix par la fonction fillvoice ; chaque onde et bruit a une entrée d'amplification à 16 bits
//      les contrôles
//          les contrôles sont des sorties en 16 bits signés (lfos, adsr, mux, switchs etc)
//          ils sont recadrés selon le type de l'entrée à laquelle ils sont appliqués (par ex, une fréquence est sur 13 bits, un rc sur 5 bits, une durée sur 7 bits, les binaires (touchb) 0/1)
//          comme déjà dit, coders et sorties atténuées sont "ajoutés" selon le type d'entrée
//          le nombre maxi de sorties possibles est fixe pour tous les objets et excédentaire
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
//      mixers  =   mélangeurs audio ou de controles (atténuateurs pour chaque entrée, entrée 0=gain général ; ampli de sortie pour les audio)
//      séquenceurs = générateurs d'impulsions et/ou valeurs 16bits programmables
//      générateurs d'écho = délai, injection, niveau
//      générateurs de réverbération, durée, brillance, niveau
//
// Ajouter un objet nécessite plusieurs interventions :
//      créer sa description (structure comme voice ou tables comme lfo)
//      l'ajouter à la liste dans objects.def
//      créer les 2 fichiers *names.def pour décrire ses entrées et sorties
//      (ajouter pour chaque entrée un nom de type dans norm_types.def)
//      ajouter dans const.h
//              son TOTAL_xxx dans les lignes TOTAL_INPUTS_REAL et TOTAL_OUTPUTS_REAL
//              un paragraphe enum d'entrées et de sorties
//      ajouter ci-aprés
//              un paragraphe nom des entrées et des sorties 
//              un paragraphe d'init dans init_objects_inputs et init_objects_outputs 
//      ajouter le traitement d'update dans update_inputs
//      ajouter un case dans menu_line_dsp qui gère la mise à jour des coders
//      ajouter dans le fichier qui le reçoit
//              la fonction d'init de ses variables
//              les fonctions setxxxxyyyy vues plus haut
//              un handler
//      ajouter l'appel de son init dans setup()

/* ************ control inputs and outputs ************* */

// every control input of every object (including unuseful inputs)
//    has an unique id which gives access to its parameters (value, source, norm, name etc)
// every object type has an [object_type]_ctl_input_id table with [object_type#][input#] elements 
// (the ids table for the inputs of every objects of this type)
// object inputs are described in files [object_type]_input_names.def (ex: lfos_inputs_names.def)
// ex: lfo#3 input#1 has the id : lfo_ctl_input_id[3][1] which is the index in the tables ctl_input_xxx[]
// or: lfo_ctl_input_id[3][LCRA]
// table ctl_input_id_chain[] allows to chain the inputs which are connected to the same output for faster access

int16_t ctl_input_val[TOTAL_INPUTS_ID_REAL];                       // current value of every input
int16_t ctl_input_prev_val[TOTAL_INPUTS_ID_REAL];                  // all inputs prev values for trig level identification
int16_t ctl_input_srce[TOTAL_INPUTS_ID_REAL];                      // all inputs sources ie outputs used for inputs (useful for disconnection) 
//uint8_t ctl_input_norm[TOTAL_INPUTS_ID_REAL]];                      // all inputs norm type (0 nothing ; 1 lfo_freq ; 2 vce freq ; 3 rc ; 4 ampl 0-31 etc)
int16_t ctl_input_shft[TOTAL_INPUTS_ID_REAL];                      // all inputs values shift type (0 no shift ; in)  ??????? input shifting not yet implemented
uint8_t ctl_input_trig[TOTAL_INPUTS_ID_REAL];                      // all inputs trig type ; reset when read
int16_t ctl_input_tlev[TOTAL_INPUTS_ID_REAL];                      // all inputs trig level
char    ctl_input_name[TOTAL_INPUTS_ID_REAL][IN_OUT_NAME_LEN];     // all inputs names
uint8_t ctl_input_update_type[TOTAL_INPUTS_ID_REAL];               // all inputs update specific job
uint8_t ctl_input_object[TOTAL_INPUTS_ID_REAL];                    // all inputs object#

int16_t ctl_input_id_chain[TOTAL_INPUTS_ID_REAL];                  // next id(input) with same output (-1/NO_LINK if nothing)

uint8_t norm_dividers[]={0,16-5,16-6,16-3};              // to add ; not used for now

// pour retrouver les id de toute input ou output, l'init d'icelles charge le numéro du premier id de chaque object

int16_t objects_first_input_id[OBJECT_TYPES_NB];
int16_t objects_first_output_id[OBJECT_TYPES_NB];

// every control output of every object has an unique id which gives access to the name and input link chain of the output
// every object type has an [object]_ctl_output_id table with [object_type#][output#] elements
// (ids table for the outputs of every objects of this type)
// object outputs are described in files [object_type]_output_names.def (ex: lfos_outputs_names.def)
// ex: lfo#2 output#3 has the id : lfo_ctl_output_id[2][3] which is the index in the tables ctl_output_xxx[] 
// or: lfo_ctl_output_id[2][LSAW]
// table ctl_output_id_chain[] (one element per output) chains the inputs connected to the same output for faster access
// contains first input id of the chain or -1/NO_LINK

/* ************** fichiers *.def des enum *************** */

// les objects sont listés dans le fichier objects.def
// chaque objet a une liste de ses entrées (0-n) avec un mnémo associé pour l'utilitaire de cablage [objet]_inputs_names.def
// pareil pour ses sorties [objet]_outputs_names.def
// une autre liste concerne les procédures de mise à jour des valeurs des entrées

char      ctl_output_name[MAX_OUTPUTS][IN_OUT_NAME_LEN];    // all objects outputs names
int16_t   ctl_output_id_chain[MAX_OUTPUTS];                 // all objects outputs chain to input (first link)
uint8_t   ctl_output_objects_nb[OBJECT_TYPES_NB];

// ***** objets *****

const char objects_names[][OBJECTS_NAME_LEN]={         
    #define Z(name,text) text,
    #include "objects.def"   
    #undef Z   
};

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

const char rev_inputs_names[][OBJ_IO_NAME_LEN]={
    #define X(name,text) text,
    #include "rev_inputs_names.def"   
    #undef X   
};

const char echo_inputs_names[][OBJ_IO_NAME_LEN]={
    #define X(name,text) text,
    #include "echo_inputs_names.def"   
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
    for (uint8_t outs=0;outs<MAX_OUTPUTS_PER_OBJ;outs++)
    {
        for(uint8_t lfm=0;lfm<MAX_LFM;lfm++)
        {        
            ctl_output_id_chain[curr_output]=NO_LINK;
            lfm_ctl_output_id[outs][lfm]=curr_output;
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

    printf("init_objects_outputs(%u)\n",curr_output-1);

    return true;
}

uint8_t init_objects_inputs(void)
{
    uint8_t inputNum=3;
    uint8_t objName=3;
    uint8_t objNum=2;
    uint8_t objOutType=3;
    uint8_t lenInpName=objName+objNum+objOutType;

    memset(ctl_input_name,0x00,sizeof(ctl_input_name));
    memset(ctl_input_srce,0x00,sizeof(ctl_input_srce));
    memset(ctl_input_shft,0x00,sizeof(ctl_input_shft));
    memset(ctl_input_trig,0x00,sizeof(ctl_input_trig));
    memset(ctl_input_tlev,0x00,sizeof(ctl_input_tlev));
    
    for (uint16_t i=0;i<TOTAL_INPUTS_ID_REAL;i++){
        ctl_input_id_chain[i]=NO_LINK;
        ctl_input_val[i]=0;
    }

    memset(ctl_input_name[0],'-',objName);
    int16_t curr_input=1;

    objects_first_input_id[LFO______]=curr_input;
    for (uint8_t lfo=0;lfo<MAX_LFO;lfo++)
    {
        for(uint8_t ins=0;ins<LFO_INPUTS_NB;ins++)
        {
            if(curr_input>=TOTAL_INPUTS_ID_REAL){return 1;}             // checked before any write: curr_input must be a valid index
            lfo_ctl_input_id[lfo][ins]=curr_input;
            ctl_input_id_chain[curr_input]=NO_LINK;
            ctl_input_object[curr_input]=lfo;
            switch(ins){
                case LFRQ:ctl_input_update_type[curr_input]=LFO_FREQ;break;
                case LCR_:ctl_input_update_type[curr_input]=LFO_CRA;break;
            }
            
            //if(ins<LFO_INPUTS_NB){
                char buf[IN_OUT_NAME_LEN]={'L','F','O','S'};    // see objName value
                convIntToString(buf+objName,lfo,objNum);
                memcpy(buf+objName+objNum,&lfo_inputs_names[ins],objOutType);  //OBJ_IO_NAME_LEN-1);
                memcpy(ctl_input_name[curr_input],buf,lenInpName);   //IN_OUT_NAME_LEN);
            //}
            curr_input++;
        }
    }
    //printf(" lfo_curr_input  :%u %u\n",objects_first_input_id[LFO______],curr_input-1);

    objects_first_input_id[ADSR_____]=curr_input;
    for (uint8_t adsr=0;adsr<MAX_ADSR;adsr++)
    {
        for(uint8_t ins=0;ins<ADSR_INPUTS_NB;ins++) 
        {
            if(curr_input>=TOTAL_INPUTS_ID_REAL){return 2;}             // checked before any write
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
            //if(ins<ADSR_INPUTS_NB){
                char buf[IN_OUT_NAME_LEN]={'A','D','S','R'};    // see objName value
                convIntToString(buf+objName,adsr,objNum);
                memcpy(buf+objName+objNum,&adsr_inputs_names[ins],objOutType);    //OBJ_IO_NAME_LEN-1);
                memcpy(ctl_input_name[curr_input],buf,lenInpName);   //IN_OUT_NAME_LEN);
            //}
            curr_input++;
        }
    }
    //printf(" adsr_curr_input :%u %u\n",objects_first_input_id[ADSR_____],curr_input-1);

    objects_first_input_id[VOICE____]=curr_input;
    for (uint8_t vce=0;vce<MAX_VOICES;vce++)
    {
        for(uint8_t ins=0;ins<VOICES_INPUTS_NB;ins++)
        {
            if(curr_input>=TOTAL_INPUTS_ID_REAL){return 3;}             // checked before any write
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
                case VFIL:ctl_input_update_type[curr_input]=VCE_FILT;break;
                case VRES:ctl_input_update_type[curr_input]=VCE_RES;break;
                case VREV:ctl_input_update_type[curr_input]=REV_V__;break;
                case VECH:ctl_input_update_type[curr_input]=ECHO_V_;break;
                default:break;
            }
            //if(ins<VOICES_INPUTS_NB){
                char buf[IN_OUT_NAME_LEN]={'V','C','E','S'};    // see objName value
                convIntToString(buf+objName,(uint32_t)vce,objNum);
                memcpy(buf+objName+objNum,&voices_inputs_names[ins],objOutType);  //OBJ_IO_NAME_LEN-1);
                memcpy(ctl_input_name[curr_input],buf,lenInpName);   //IN_OUT_NAME_LEN);
            //}
            curr_input++;
        }
    }
    //printf(" voice_curr_input:%u %u\n",objects_first_input_id[VOICE____],curr_input-1);

    objects_first_input_id[LF_MUX___]=curr_input;
    for (uint8_t ins=0;ins<LFM_INPUTS_NB;ins++)
    {
        for(uint8_t lfm=0;lfm<MAX_LFM;lfm++)
        {
            if(curr_input>=TOTAL_INPUTS_ID_REAL){return 4;}             // checked before any write
            lfm_ctl_input_id[ins][lfm]=curr_input;           
            ctl_input_id_chain[curr_input]=NO_LINK;
            ctl_input_object[curr_input]=lfm;
            ctl_input_update_type[curr_input]=LFM____;
            //ctl_input_val[curr_input]=0;  déjà fait
            
            //if(ins<MAX_LFM_INPUTS){
                char buf[IN_OUT_NAME_LEN]={'L','F','M','_'};    // see objName value
                convIntToString(buf+objName,lfm,objNum);
                *(buf+objName+objNum)='_';
                convIntToString(buf+objName+objNum+1,ins,2);
                memcpy(ctl_input_name[curr_input],buf,lenInpName);   //IN_OUT_NAME_LEN);
            //}
            curr_input++;
        }
    }

    objects_first_input_id[REVERB___]=curr_input;
    for (uint8_t ins=0;ins<REVB_INPUTS_NB;ins++) 
    {
        for(uint8_t rev=0;rev<MAX_REV;rev++)
        {
            if(curr_input>=TOTAL_INPUTS_ID_REAL){return 5;}             // checked before any write
            rev_ctl_input_id[ins][rev]=curr_input;           
            ctl_input_id_chain[curr_input]=NO_LINK;
            ctl_input_object[curr_input]=rev;
            switch(ins){
                case REVD:ctl_input_update_type[curr_input]=REV_D__;break;
                case REVS:ctl_input_update_type[curr_input]=REV_S__;break;
                case REVR:ctl_input_update_type[curr_input]=REV_R__;break;
            }
            //ctl_input_val[curr_input]=0;  déjà fait
            
            //if(ins<MAX_REV_INPUTS){
                char buf[IN_OUT_NAME_LEN]={'R','E','V','_'};    // see objName value
                convIntToString(buf+objName,rev,objNum);
                *(buf+objName+objNum)='_';
                convIntToString(buf+objName+objNum+1,ins,2);
                memcpy(ctl_input_name[curr_input],buf,lenInpName);   //IN_OUT_NAME_LEN);
            //}
            curr_input++;
        }
    }    
        
    objects_first_input_id[ECHOS____]=curr_input;
    for (uint8_t ins=0;ins<ECHO_INPUTS_NB;ins++)
    {
        for(uint8_t echo=0;echo<MAX_ECHO;echo++)
        {
            if(curr_input>=TOTAL_INPUTS_ID_REAL){                         // checked before any write
                printf("%u>=%u\n",curr_input,TOTAL_INPUTS_ID_REAL);
                return 6;}
            echo_ctl_input_id[ins][echo]=curr_input;           
            ctl_input_id_chain[curr_input]=NO_LINK;
            ctl_input_object[curr_input]=echo;
            switch(ins){
                case ECHD:ctl_input_update_type[curr_input]=ECHO_D_;break;
                case ECHI:ctl_input_update_type[curr_input]=ECHO_I_;break;
                case ECHR:ctl_input_update_type[curr_input]=ECHO_R_;break;
            }
            //ctl_input_val[curr_input]=0;  déjà fait
            
            //if(ins<MAX_ECHO_INPUTS){
                char buf[IN_OUT_NAME_LEN]={'E','C','H','_'};    // see objName value
                convIntToString(buf+objName,echo,objNum);
                *(buf+objName+objNum)='_';
                convIntToString(buf+objName+objNum+1,ins,2);
                memcpy(ctl_input_name[curr_input],buf,lenInpName);   //IN_OUT_NAME_LEN);
            //}
            curr_input++;
        }
    } 

    printf("init_obects_inputs(%u)\n",curr_input-1);

    // ajouter ici pour les entrées d'autres objets  (sequencers etc)
    
    if(curr_input!=TOTAL_INPUTS_ID_REAL){system_error("input count",curr_input);}      // short message: it is drawn on the display

    return 0;
}  

static spin_lock_t *inputs_id__lock;

void objects_table_init()
{
    inputs_id__lock = spin_lock_init(CTL_INPUTS_LOCK);

    uint8_t e=init_objects_inputs();
    if(e!=0){system_error("_init_objects_inputs",e);}
    if(!init_objects_outputs()){system_error("_init_objects_outputs");}
}

void __not_in_flash_func(connect_input)(uint16_t input_id, uint16_t output_id)     // add input_id at end of output_id_chain * setup ctl_input_srce[input_id] to output_id
{
    uint32_t f = spin_lock_blocking(inputs_id__lock);

    // link init only (norm/shft/trig/tlev update by menu_mapping)
    // make sure chain ends here
    printf("\nctl_output_id_chain[%u]=%i ",output_id,ctl_output_id_chain[output_id]);
    ctl_input_id_chain[input_id] = NO_LINK;

    int16_t next_id = ctl_output_id_chain[output_id];

    if(next_id==NO_LINK)
        {ctl_output_id_chain[output_id]=input_id;
        ctl_input_srce[input_id]=output_id;
        //printf("->%u\n",input_id);
        } // end of update

    else {
        int16_t prev=next_id;                   // first id of the chain from ctl_output_id_chain

        while (next_id != NO_LINK) {            // end of chain ? 
            prev=next_id;
            next_id=ctl_input_id_chain[prev];
            //printf(" ciic[%i]=%i ",prev,next_id);
            if(next_id>TOTAL_INPUTS_REAL || next_id<NO_LINK){
                spin_unlock(inputs_id__lock, f);
                system_error("input_id overflow c",next_id);}
        }        
        ctl_input_id_chain[prev]=input_id;      // update end of chain
        ctl_input_srce[input_id]=output_id;     // update output_id of the last input of the chain
        //printf(" ciic[%i]->%i \n",prev,ctl_input_id_chain[prev]);
    }        

    spin_unlock(inputs_id__lock, f);

    printf("out#:%d in_id:%d out_id_chain:%i inp_id_chain:%i\n",output_id,input_id,ctl_output_id_chain[output_id],ctl_input_id_chain[input_id]);
}

void __not_in_flash_func(disconnect_input)(uint16_t input_id, uint16_t output_id)
{
    if(output_id==0 || output_id>=MAX_OUTPUTS){return;}

    uint32_t f = spin_lock_blocking(inputs_id__lock);
    int16_t first = ctl_output_id_chain[output_id];

    // chaîne vide → rien à faire
    if (first == NO_LINK) {
        ctl_input_id_chain[input_id] = NO_LINK;
        ctl_input_srce[input_id] = NO_LINK;
        spin_unlock(inputs_id__lock, f);
        return;
    }
    // cas 1 : le maillon à retirer est en tête
    if (first == input_id) {
        ctl_output_id_chain[output_id] = ctl_input_id_chain[input_id];   // nouveau head = suivant
        ctl_input_id_chain[input_id] = NO_LINK;
        ctl_input_srce[input_id] = NO_LINK;                  
        spin_unlock(inputs_id__lock, f);
        return;
    }

    // cas 2 : maillon au milieu / fin
    int16_t prev = first;
    int16_t curr = ctl_input_id_chain[first];
   
    if(prev>TOTAL_INPUTS_REAL || prev<NO_LINK || curr>TOTAL_INPUTS_REAL || curr<NO_LINK){
        spin_unlock(inputs_id__lock, f);
        system_error("input_id overflow d1");}

    while (curr != NO_LINK && curr != input_id) {
        prev = curr;
        curr = ctl_input_id_chain[curr];
        
        if(curr>TOTAL_INPUTS_REAL || curr<NO_LINK){
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

void __not_in_flash_func(update_inputs)(int16_t id,int16_t valeur)  // inputs update with valeur (any object output)
                                                                    // valeur est la valeur linéaire à atténuer sur 16 bits recadrée selon le type d'entrée
                                                                    // id numéro unique de l'input donne accès à toutes ses caractéristiques
                                                                    //  le ptr dans ctl_input_id_chain de l'id suivant recevant la meme output/valeur
                                                                    //  la valeur du coder associé
                                                                    //  le type d'entrée via ctl_input_update_type
                                                                    //  la valeur précédente de l'entrée via ctl_input_val
{
//uint32_t ints = save_and_disable_interrupts();gpio_put(TST_PIN,1);

        int16_t tlev;

        while(id!=NO_LINK){

            int16_t prev = ctl_input_val[id];
            ctl_input_val[id] = valeur;
            uint8_t object=ctl_input_object[id];

            switch(ctl_input_update_type[id]){
                case NOTHING_:  break;
                case VCE_FREQ:  setVoiceFrequency(&voices[object]);break;
                case VCE_CRA :  setVoiceCycleR(&voices[object]);break;
                case VCE_WSIN:  setVoiceAmpl(&voices[object],WSIN);break;
                case VCE_WTRI:  setVoiceAmpl(&voices[object],WTRI);break;
                case VCE_WSAW:  setVoiceAmpl(&voices[object],WSAW);break;
                case VCE_WSQR:  setVoiceAmpl(&voices[object],WSQR);break;
                case VCE_WHIT:  setVoiceAmpl(&voices[object],WHIT);break;
                case VCE_WPNK:  setVoiceAmpl(&voices[object],PONK);break;
                case VCE_GENA:  setVoiceAmpl(&voices[object],BASIC_WAVES_NB);break;
                case VCE_FILT:  setVoiceFilterFrequency(&voices[object]);break;         // cutoff code = coder + attenuated input (same shift of 6 as before)                         
                case VCE_RES :  setVoiceFilterRes(&voices[object]);break;
                case ECHO_V_ :  setVoiceEcho(&voices[object]);break;                    // echo send level of the voice
                case ECHO_D_ :                                                          // delay, feedback, return: setEcho computes all three
                case ECHO_I_ :
                case ECHO_R_ :  setEcho(object);break;                
                case LFO_FREQ:  setLfosFrequency(object);break;                         // 8k max VCES_MAX_FREQ_CODERS ; att 0-255 
                case LFO_CRA :  setLfosCycleR(object); break;
                case A_ATTACK:  setAdsrDur(object,ADSR_ATT);break;
                case A_DECAY :  setAdsrDur(object,ADSR_DEC);break;
                case A_SUST  :  setAdsrDur(object,ADSR_SUS);break;
                case A_RELEAS:  setAdsrDur(object,ADSR_REL);break;
                case A_LEVEL :  setAdsrLev(object);break;                       
                case A_START :  ctl_input_prev_val[id]=prev;                            // start adsr
                                tlev=ctl_input_tlev[id];
                                switch(ctl_input_trig[id]){
                                    case INP_NO_TRIG:break;
                                    case INP_UP_TRIG:
                                        if(valeur>=tlev && prev<tlev){
                                            adsrStatus[object]=ADSR_ATT;
                                            adsrCurrEch[object]=0;
                                        }break;
                                    case INP_DOWN_TRIG:
                                        if(valeur<=tlev && prev>tlev){
                                            adsrStatus[object]=ADSR_ATT;
                                            adsrCurrEch[object]=0;
                                        }break;
                                    case INP_U_D_TRIG:
                                        if((valeur>tlev && prev<tlev) || (valeur<=tlev && prev>tlev)){
                                            adsrStatus[object]=ADSR_ATT;
                                            adsrCurrEch[object]=0;
                                        }break;
                                    default:break;
                                }break;
                case LFM____ :  {                                // lfm #
                                setLfm(object);
                                }break;
                default: break;
            }
            id = ctl_input_id_chain[id];
        }
}        
