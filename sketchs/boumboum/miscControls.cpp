#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "pico/stdlib.h"
#include "const.h"
#include "util.h"
#include "miscControls.h"
#include "input_tables_management.h"

/* ****** adsr ****** */

uint16_t adsrCoderAtt[MAX_ADSR];
uint16_t adsrCoderDec[MAX_ADSR];
uint16_t adsrCoderSus[MAX_ADSR];
uint16_t adsrCoderRel[MAX_ADSR];
uint16_t adsrCoderLev[MAX_ADSR];
uint16_t adsrCoderAttAtt[MAX_ADSR];
uint16_t adsrCoderDecAtt[MAX_ADSR];
uint16_t adsrCoderSusAtt[MAX_ADSR];
uint16_t adsrCoderRelAtt[MAX_ADSR];
uint16_t adsrCoderLevAtt[MAX_ADSR];
uint8_t  adsrStatus[MAX_ADSR];
uint32_t adsrDurAtt[MAX_ADSR];
uint32_t adsrDurDec[MAX_ADSR];
uint32_t adsrDurSus[MAX_ADSR];
uint32_t adsrDurRel[MAX_ADSR];
uint32_t adsrCurrEch[MAX_ADSR];
uint16_t adsrOutputsValues[MAX_ADSR][MAX_OUTPUTS_PER_OBJ];  // inutilisé ??
int16_t  adsr_ctl_input_id[MAX_ADSR];
int16_t  adsr_ctl_output_id[MAX_ADSR][MAX_OUTPUTS_PER_OBJ];
int32_t  adsrScopeBufReal[MAX_ADSR*ADSR_SCOPE_BUFFER_LEN];  // real values
uint16_t adsrScopeBufPtr[MAX_ADSR];

uint32_t    adsrTime=0;
uint32_t    adsrTimingInterval=1000/ADSR_SAMPLE_RATE;

extern uint32_t millisCounter;
extern uint16_t amplLevel[];
extern int16_t  ctl_output_id_chain[];
extern uint8_t  ctl_input_update_type[];

bool adsrScopeDisp[MAX_ADSR];

/* ******* touchButtons ******* */

int16_t  tbut_ctl_output_id[MAX_TBUT][MAX_OUTPUTS_PER_OBJ];

void adsrInit()
{
    printf("%u adsr init\n",MAX_ADSR);
    for(uint8_t a=0;a<MAX_ADSR;a++){

        adsrDurAtt[a]=0;
        adsrDurDec[a]=0;
        adsrDurSus[a]=0;
        adsrDurRel[a]=0;

        adsrCoderAtt[a]=6;setAdsrDur(a,ADSR_ATT,adsrDurAtt[a]);
        adsrCoderDec[a]=28;setAdsrDur(a,ADSR_DEC,adsrDurDec[a]);
        adsrCoderSus[a]=12;setAdsrDur(a,ADSR_SUS,adsrDurSus[a]);
        adsrCoderRel[a]=96;setAdsrDur(a,ADSR_REL,adsrDurRel[a]);

        adsrCoderLev[a]=31;setAdsrLev(a,adsrCoderLev[a]);
        adsrStatus[a]=ADSR_OFF;

        adsrCurrEch[a]=0;

        adsrScopeBufPtr[a]=0;
                        
        adsrCoderAttAtt[a]=FULL_ATTENUATION_VALUE;
        adsrCoderDecAtt[a]=FULL_ATTENUATION_VALUE;
        adsrCoderSusAtt[a]=FULL_ATTENUATION_VALUE;
        adsrCoderRelAtt[a]=FULL_ATTENUATION_VALUE;
        adsrCoderLevAtt[a]=FULL_ATTENUATION_VALUE;

        for(uint8_t v=0;v<MAX_OUTPUTS_PER_OBJ;v++){adsrOutputsValues[a][v]=0;}
    }
}

#define ONE_Q16     65536
#define RC_SAMPLES_NB  2048
#define P16 (1<<16) 

const uint32_t yTableQ16[DUR_ECH_NB] = {
//1,2,3,4,5,7,8,10,11,13,15,18,21,24,27,31,35,39,44,49,55,62,69,76,84,93,103,114,125,137,151,165,181,197,215,235,256,278,302,328,356,
//386,418,452,488,527,569,614,661,712,766,824,886,952,1021,1096,1175,1259,1348,1443,1544,1651,1765,1886,2014,2149,2293,2445,2606,2777,
//2957,3148,3350,3564,3790,4028,4281,4547,4828,5124,5438,5768,6116,6483,6871,7279,7709,8162,8639,9141,9669,10226,10811,11427,12074,
//12755,13471,14223,15014,15845,16717,17634,18596,19606,20666,21778,22946,24170,25454,26801,28213,29693,31245,32870,34574,36358,
//38227,40184,42233,44379,46624,48974,51434,54007,56698,59514,62458,65532
1,2,2,3,4,4,5,6,7,8,9,10,11,12,14,15,17,18,20,22,24,26,29,31,34,36,39,42,46,49,53,57,61,66,
71,76,81,86,92,98,105,112,119,127,135,143,152,162,172,182,193,204,216,229,242,256,271,286,302,318,336,354,373,394,414,436,459,483,508,535,
562,591,620,652,684,718,753,790,829,869,911,954,999,1047,1096,1147,1201,1256,1314,1374,1437,1502,1570,1640,1713,1789,1868,1951,2036,2125,
2217,2313,2412,2515,2622,2734,2849,2969,3093,3222,3356,3495,3638,3788,3942,4103,4269,4441,4620,4804,4996,5195,5400,5613,5834,6062,6298,6543
};

uint32_t stepTableQ16[DUR_ECH_NB];              // coefficient RC par durée (Q16)
uint16_t rcCurve[RC_SAMPLES_NB];                // courbe RC 0..32763 pour compatibilié avec autres sorties en int16_t  

const uint32_t stepMinQ16 = 32768;                    // 0.5 en Q16
const uint32_t stepMaxQ16 = (RC_SAMPLES_NB << 16);    // RC_SAMPLES_NB en Q16

void fillDur(void)
{
    // stepTableQ16 : transfo des DUR_ECH_NB valeurs linéaires en courbe +-log pour ergonomie coder
    // production yTableQ16 voir tableau excel
    const uint32_t yMax = yTableQ16[DUR_ECH_NB-1];      // 65532
    for (uint32_t d = 0; d < DUR_ECH_NB; d++) {
        uint32_t y = yTableQ16[d];
        if (y == 0) y = 1;

        // step en Q16 = (RC_SAMPLES_NB / y) en Q0
        // donc stepQ16 = (RC_SAMPLES_NB << 16) / y
        stepTableQ16[d] = ((uint32_t)RC_SAMPLES_NB << 16) / y;
        //printf("%u %u\n",d,stepTableQ16[d]);
    }  


// rcCurve : approximation tension condensateur RC
    
// formule générale pour obtenir la table 
//  1) kQ16 détermine le creux
//  k = 1-(1-taux)^(1/RC_SAMPLES_NB)
//  kQ16 = arrondi (k*65536)
//  ex: taux = 0.9 ; RC_SAMPLES_NB = 2048
//  k = 1-0.1^(1/2048) = 0.0011237
//  kQ16 = 73.64 soit 74
//  ex: taux = 0.91 ; RC_SAMPLES_NB = 2048
//  k = 1-0.09^(1/2048) = 0.001175
//  kQ16 = 77
//  ex: taux = 0.93 ; RC_SAMPLES_NB = 2048
//  k = 1-0.07^(1/2048) = 0.001297
//  kQ16 = 85
//  2) ONE_Q16_XX le taux de la courbe parcouru 
//  ONE_Q16=65536 ; ONE_Q16_xx = ONE_Q16 / taux
//  ex: taux = 0.9 ; ONE_Q16_09 = 65536 / 0.9 = 72817
//  ex: taux = 0.91 ; ONE_Q16_091 = 65536 / 0.91 = 72017 ajusté à 72440
//  ex: taux = 0.93 ; ONE_Q16_093 = 65536 / 0.93 = 70469 ajusté à 70858
//  la valeur finale peut être ajustée pour que le dernier step amène aussi près que possible de 65536 (voir tableau excel)
 
    #define ONE_Q16_90  72440 // 65536 / 0.905 ; 65536 * 1/0.90   
    #define ONE_Q16_93  70858
    #define ONE_Q16_RC ONE_Q16_93
    #define KQ16_90 77   // Pour coeff =0.90 et 2048 samples : 𝑘 = 1-0,091/2048≈0,0011756 𝑘𝑄16≈0,0011756*65536≈77
    #define KQ16_93 85
    #define KQ16 KQ16_93

    uint32_t yQ16 = 0;    

    for (int i = 0; i < RC_SAMPLES_NB; i++)
    {
        uint32_t diff = ONE_Q16_RC - yQ16;
        uint32_t dy   = (uint32_t)(((uint64_t)diff * KQ16) >> 16);
        yQ16 += dy;
        if (yQ16 > ONE_Q16_RC) yQ16 = ONE_Q16_RC;

        rcCurve[i] = (uint16_t)((yQ16 * 65535ULL) >> 16);
        //printf("%i %u\n",i,rcCurve[i]);
    }
}

 //printf("%i %u\n",d,stepTable[d]);
 //printf("%i %u\n",i,rcCurve[i]);


void __not_in_flash_func(setAdsrLev)(uint8_t adsr,int32_t val)
{
    adsrCoderLev[adsr]=val;
}

void __not_in_flash_func(setAdsrDur)(uint8_t adsr,uint8_t adsrStatus,int32_t val)
{
    uint32_t* vv;
    uint32_t v0;

    switch(adsrStatus){
        case ADSR_ATT:vv=&adsrDurAtt[adsr];v0=adsrCoderAtt[adsr]+val;break;
        case ADSR_DEC:vv=&adsrDurDec[adsr];v0=adsrCoderDec[adsr]+val;break;
        case ADSR_SUS:vv=&adsrDurSus[adsr];v0=adsrCoderSus[adsr]+val;break;
        case ADSR_REL:vv=&adsrDurRel[adsr];v0=adsrCoderRel[adsr]+val;break;
        default:break;
    }
    if (v0 >= DUR_ECH_NB){v0 = DUR_ECH_NB - 1;}


    *vv=stepTableQ16[v0];

    //printf("setAdsrDur %u:%u %i *vv:%u v0:%u ce:%u\n",adsr,adsrStatus,val,*vv,v0,adsrCurrEch[adsr]);
}

uint16_t adsrNext(uint32_t* currEchQ16,uint32_t stepQ16)    // production indice suivant dans rcCurve selon dur
{
    // avance
    uint32_t next = *currEchQ16 + stepQ16;

    // clamp fin de courbe
    uint32_t maxQ16 = ((RC_SAMPLES_NB-1) << 16);
    if (next >= maxQ16){next = maxQ16;}

    *currEchQ16 = next;

    // conversion Q16 → index entier
    return (uint16_t)(next >> 16);
}

void __not_in_flash_func(adsrHandler)()
{
    if((millisCounter-adsrTime)>adsrTimingInterval){
        adsrTime=millisCounter;

        for(uint8_t a=0;a<MAX_ADSR;a++)
        {
            int16_t   bol=0;
            uint16_t  ov_=0;            // analog level output value
            uint32_t  ov0;
            uint8_t*  as=&adsrStatus[a];
            uint16_t* ap=&adsrScopeBufPtr[a];
            if (__builtin_expect(*as != ADSR_OFF, 0)) {

                bol=0x7fff;
                uint16_t  lev=amplLevel[adsrCoderLev[a]];
                uint32_t* ce=&adsrCurrEch[a];            
                uint32_t  cx=*ce>>16;   // prev currEch  
                
                //if(a==1){printf("%u:%u \n",a,*as);}

                // !!!!!!!!!!!! rcCurve fournit des valeurs 0-0xffff et update_inputs prend des valeurs 0x7fff !!!!!!!!!!!!!
                
                switch(*as){
                    case ADSR_ATT:
                        cx=adsrNext(ce,adsrDurAtt[a]);
                        ov_=rcCurve[cx]/2;    // produit des valeurs 0 -> 0xffff (uint) ; limiter à 0x7fff pour compatibilité update_inputs)

                        if (cx == RC_SAMPLES_NB-1) { *ce = 0; *as = ADSR_DEC;}
                        break;

                    case ADSR_DEC:
                        // valeurs 1-x
                        cx=adsrNext(ce,adsrDurDec[a]);
                        ov0=rcCurve[cx];
                        ov_  = (uint16_t)(((uint32_t)P16 - ((uint32_t)(P16 - lev) * (uint32_t)ov0)/ (uint32_t)P16)/2);

                        if (cx == RC_SAMPLES_NB-1) {*ce = 0; *as = ADSR_SUS;}                        
                        break;

                    case ADSR_SUS:
                        cx=adsrNext(ce,adsrDurSus[a]);  // timing management                    
                        //*ov=lev;
                        ov_=lev/2;

                        if (cx == RC_SAMPLES_NB-1) { *ce = 0; *as = ADSR_REL;}
                        break;

                    case ADSR_REL:            
                        // valeurs 1-x
                        cx=adsrNext(ce,adsrDurRel[a]);
                        ov0=rcCurve[cx];
                        ov_ = (uint16_t)(((uint32_t)lev - (uint32_t)((uint32_t)lev * (uint32_t)ov0) / (uint32_t)P16)/2);

                        if (cx == RC_SAMPLES_NB-1) { *ce = 0; *as = ADSR_OFF;ov_=0;} 
                        break;

                    default:break;
                }                
//if(a==1){printf("%i\n",adsr_ctl_output_id[a]);}
                // update connected level inputs
                int16_t in_lev_id=ctl_output_id_chain[adsr_ctl_output_id[a][ADSR_SHAPE]];
                if (__builtin_expect(in_lev_id != NO_LINK, 0)){
                    ov_ &= 0x7fff; // écrêtage
                    adsrOutputsValues[a][ADSR_SHAPE]=ov_;
//printf("%u %u %i %u\n",a,ov_,in_lev_id,ctl_input_update_type[in_lev_id]);
                    update_inputs(in_lev_id,ov_);
                }

                //if(a==0){printf("%u\n",ov_);}
                adsrScopeBufReal[a*ADSR_SCOPE_BUFFER_LEN + *ap]=ov_;

                (*ap)++;  // until ADSR_OFF
            }
            else if(__builtin_expect(*ap!=0 && *ap<(ADSR_SCOPE_BUFFER_LEN-1),0)){       // effacement fin de courbe
                int32_t* ab=&adsrScopeBufReal[a*ADSR_SCOPE_BUFFER_LEN];
                for(uint16_t x=*ap;x<ADSR_SCOPE_BUFFER_LEN;x++){*(ab+x)=0;}
                *ap=0;adsrScopeDisp[a]=true;   
            }
        
            if(__builtin_expect(*ap>=ADSR_SCOPE_BUFFER_LEN,0)){*ap=0;adsrScopeDisp[a]=true;}
        
            // update connected bool inputs
            int16_t in_bool_id=ctl_output_id_chain[adsr_ctl_output_id[a][ADSR_GATE]];
            if (__builtin_expect(in_bool_id != NO_LINK, 0)){
                adsrOutputsValues[a][ADSR_GATE]=bol;                
                update_inputs(in_bool_id,bol);
            }
        }
    }
}

void irq_button_init(uint8_t pin)
{
    gpio_init(pin);gpio_set_function(pin, GPIO_FUNC_SIO);gpio_set_dir(pin,GPIO_IN);
    gpio_init(BUT_VCC_PIN);gpio_set_function(BUT_VCC_PIN, GPIO_FUNC_SIO);gpio_set_dir(BUT_VCC_PIN,GPIO_OUT);
    gpio_put(BUT_VCC_PIN,LOW);sleep_ms(100);gpio_put(BUT_VCC_PIN,HIGH);
    gpio_irq_init(pin);  // après  init_global_dma_irq();
}

void touch_button_init(uint8_t pin)
{
    gpio_init(pin);gpio_set_function(pin, GPIO_FUNC_SIO);gpio_set_dir(pin,GPIO_IN);
}

void __not_in_flash_func(touch_button_handler)(uint8_t touchButtonNb,bool* touchButtonValue,volatile bool* coderTouchB)
{
            *touchButtonValue=!(*touchButtonValue);
            printf("c:%u csw:%u \n",touchButtonNb,*touchButtonValue);
            int16_t in_bool_id=ctl_output_id_chain[tbut_ctl_output_id[touchButtonNb][0]];
            
            if (__builtin_expect(in_bool_id != NO_LINK, 0)){
                printf("%u\n",in_bool_id);                
                update_inputs(in_bool_id,*touchButtonValue *2-1);
            }
        
            if(coderTouchB!=nullptr){
                coderTouchB[touchButtonNb]=*touchButtonValue;
            }
}