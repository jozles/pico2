#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <malloc.h>
#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/timer.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/pwm.h"

#include "bb_i2s.h"
#include "const.h"
#include "tests.h"
#include "std_utils.h"
#include "coder.h"
#include "frequences.h"
#include "leds.h"
#include "st7789.h"
#include "menus.h"
#include "input_tables_management.h"
#include "sound_level_management.h"
#include "miscControls.h"

extern struct Voice voices[];

extern int16_t objects_first_input_id[OBJECT_TYPES_NB];
extern int16_t objects_first_output_id[OBJECT_TYPES_NB];

extern int16_t  ctl_input_srce[TOTAL_INPUTS_ID_REAL];
extern char     ctl_input_name[TOTAL_INPUTS_ID_REAL][IN_OUT_NAME_LEN]; 
extern char     ctl_output_name[MAX_OUTPUTS][IN_OUT_NAME_LEN];

extern float    lfosFrequency[MAX_LFO];                   
extern uint16_t lfosCodersFreq[MAX_LFO];

extern uint16_t lfmCoder[LFM_INPUTS_NB][MAX_LFM];
extern uint16_t lfmCoderAtt[LFM_INPUTS_NB][MAX_LFM];

extern int16_t  lfm_ctl_input_id[LFM_INPUTS_NB][MAX_LFM];
extern int16_t  lfm_ctl_output_id[MAX_OUTPUTS_PER_OBJ][MAX_LFM];

extern const char objects_names[][OBJECTS_NAME_LEN];

// ================= test Setup =====================

void obj_connect(int16_t inp_id,int16_t out_id)
{
    if(inp_id<=0 || inp_id>=TOTAL_INPUTS_ID_REAL || out_id<0 || out_id>=MAX_OUTPUTS){
        printf("obj_connect out of range: inp=%i out=%i\n",inp_id,out_id);
        return;
    }

    // make sure input is properly disconnected
    disconnect_input(inp_id,ctl_input_srce[inp_id]);

    // connect input on output via id chain
    connect_input(inp_id,out_id);

    char libi[IN_OUT_NAME_LEN+1];memset(libi,0x00,IN_OUT_NAME_LEN+1);
    memcpy(libi,&ctl_input_name[inp_id][0],IN_OUT_NAME_LEN);
    char libo[IN_OUT_NAME_LEN+1];memset(libo,0x00,IN_OUT_NAME_LEN+1);
    memcpy(libo,&ctl_output_name[ctl_input_srce[inp_id]],IN_OUT_NAME_LEN);    
    printf("%u:%s - %u:%s\n",inp_id,libi,out_id,libo); 
}

void sub_lfo(uint8_t object_type,uint8_t object_nb,uint8_t obj_inp_nb,uint8_t vInpNb,int8_t lfo,uint16_t coder,uint8_t lOutNb)
{
    uint8_t launcher;
    if(lfo>=0){
        launcher=LFO______;
        setLfosFrCoders(lfo,coder,0);
        setLfosFrequency(lfo);
        setLfosCycleR(lfo,MAXCODER_RC/2,0);
    }
    else {
        launcher=TBUTTON__;
        lfo=-lfo-1;    // numéro du touchb
        lOutNb=0;
    }
    
    // CX (voice)vInpNb (lfo out touche)lOutNb
    printf("(%i-%i-%i) %s:%u inp:%u lfo:%u outNb:%u f:%f",
        objects_first_input_id[VOICE____],objects_first_output_id[launcher],objects_first_output_id[ADSR_____],objects_names[object_type],object_nb,vInpNb,lfo,lOutNb,lfosFrequency[lfo]);
    obj_connect(objects_first_input_id[object_type]+object_nb*obj_inp_nb+vInpNb,objects_first_output_id[launcher]+lfo*MAX_OUTPUTS_PER_OBJ+lOutNb);
}

void voiceConfig(uint8_t voice,uint8_t wave,uint16_t coderFreq,uint8_t attenFreqLevel,uint8_t freqLfo,uint16_t freqLfoCoder,
    uint8_t manualAmpLevel,uint8_t attenAmpLevel,uint8_t adsr,int8_t adsrLfo,uint16_t adsrLfoFreqCoder,int8_t cr,uint8_t crAtt)       // adsrLfo >=0 lfo ; <0 touch
{
    setVoiceCycleRCoders(&voices[voice],cr+RC_TABLES_NB-1,crAtt);
    setVoiceAmplCoders(&voices[voice],wave,manualAmpLevel,attenAmpLevel);        // manual level, input level attenuator
    setVoiceFreqCoders(&voices[voice],coderFreq,attenFreqLevel);

    if(freqLfo!=0){sub_lfo(VOICE____,voice,VOICES_INPUTS_NB,VFRQ,freqLfo,freqLfoCoder,WTRI);}    // modulation freq de la voix

    // CX (voice)SIP (adsr)
    printf("Amp ctl adsr:%u ",adsr);
    obj_connect(objects_first_input_id[VOICE____]+voice*VOICES_INPUTS_NB+VSPW+wave,objects_first_output_id[ADSR_____]+adsr*MAX_OUTPUTS_PER_OBJ+ADSR_SHAPE);

    sub_lfo(ADSR_____,adsr,ADSR_INPUTS_NB,STAR,adsrLfo,adsrLfoFreqCoder,WTRI);                 // adsr launcher lfo/touchB
    
    printf("voice:%u coderFreq:%i frAtt:%i wave:%u Ampl:%u amplAtt:%i\n\n",
        voice,voices[voice].coderFreq,voices[voice].coderFreqAtt,wave,voices[voice].coderWaveAmpl[wave],voices[voice].coderWaveAmplAtt[wave]);    
}

void vsetup(uint8_t v,uint16_t coderFreq,uint16_t coderFreqAtt,uint16_t coderAmp,uint16_t coderAmpAtt,int8_t cr,int16_t crAtt,uint8_t wave,int16_t filterFr,int16_t filterFrAtt,int16_t filterLevAtt,int16_t filterRes,int16_t filterResAtt,uint8_t cga,uint16_t cgaa,uint8_t echo,uint8_t echoAtt)
{
    setVoiceFreqCoders(&voices[v],coderFreq,coderFreqAtt);
    setVoiceCycleRCoders(&voices[v],cr+RC_TABLES_NB-1,crAtt);
    setVoiceFilter(&voices[v],filterFr,filterFrAtt,filterLevAtt,filterRes,filterResAtt);
    setVoiceAmplCoders(&voices[v],wave,coderAmp,coderAmpAtt);                   // manual level, input level attenuator
    setVoiceAmplCoders(&voices[v],BASIC_WAVES_NB,cga,cgaa);                     // voice gain (these two parameters were ignored)
    setVoiceEchoCoders(&voices[v],echo,echoAtt);

    dumpVoices(voices,v);
}

void vsetup(uint8_t v,uint16_t coderFreq,uint16_t coderFreqAtt,uint8_t coderAmp,uint8_t  coderAmpAtt,int8_t cr,int16_t crAtt,uint8_t wave){
    vsetup(v,coderFreq,coderFreqAtt,coderAmp, coderAmpAtt,cr,crAtt,wave,coderFreq+409,0,MAX_CTL_ATT,0,0,1,0,0,0);   
}

void asetup(uint8_t adsr,uint8_t att,uint8_t dec, uint8_t sus,uint8_t rel){
        setAdsrDurCoders(adsr,ADSR_ATT,att,FULL_ATTENUATION_VALUE);
        setAdsrDurCoders(adsr,ADSR_DEC,dec,FULL_ATTENUATION_VALUE);
        setAdsrDurCoders(adsr,ADSR_SUS,sus,FULL_ATTENUATION_VALUE);
        setAdsrDurCoders(adsr,ADSR_REL,rel,FULL_ATTENUATION_VALUE);
}

/*void sound3(uint8_t voice,uint8_t vwave,uint16_t coderFreq,uint8_t coderAttF,uint8_t coderAmp,uint8_t coderAmpAtt,
        uint8_t coderCr,uint8_t crAtt,
        int8_t adsr,int8_t sourceAdsr,
        int8_t lfm,int8_t lfm1,uint8_t voiceAmpLfoLfmInput,uint8_t voiceFreqLfoLfmInput,uint8_t voiceFreqAdsrLfmInput,
        int8_t voiceAmpLfo,uint16_t ampLfoFreq,
        int8_t voiceFreqLfo,uint16_t freqLfoFreq
    )
{
    // config voice
    vsetup(voice,coderFreq,coderAttF,coderAmp,coderAmpAtt,coderCr,crAtt,vwave);                
            // connect source to adsr
    uint8_t srcAdsrOffset=0;
    uint8_t sourceType=TBUTTON__;
    uint8_t waveF=0;
    if(sourceAdsr<0){
       srcAdsrOffset=-1-sourceAdsr;
       sourceType=LFO______;
       waveF=WSIN; 
    }
    obj_connect(objects_first_input_id[ADSR_____]+adsr*ADSR_INPUTS_NB+STAR,objects_first_output_id[sourceType]+(sourceAdsr+srcAdsrOffset)*MAX_OUTPUTS_PER_OBJ+waveF);        

    // config lfo amp
    if(voiceAmpLfo>=0){
        setLfosFrCoders(voiceAmpLfo,ampLfoFreq,0);
        setLfosFrequency(voiceAmpLfo);
    }
    
    if(lfm>=0){   
        // connect lfm0 to voice:wave amp input
        obj_connect(objects_first_input_id[VOICE____]+voice*VOICES_INPUTS_NB+VSPW+vwave,objects_first_output_id[LF_MUX___]+LMUXO*MAX_LFM+lfm);
        // connect adsr to lfm0:0 genAmp input
        uint8_t adsrLfmInput=0;
        setLfmCoders(lfm,adsrLfmInput,lfmCoder[adsrLfmInput][lfm],70);   
        obj_connect(objects_first_input_id[LF_MUX___]+adsrLfmInput+lfm*LFM_INPUTS_NB,objects_first_output_id[ADSR_____]+adsr*MAX_OUTPUTS_PER_OBJ+ADSR_SHAPE);
        if(voiceAmpLfo>=0){
            // connect lfo amp to lfm0:1
            obj_connect(objects_first_input_id[LF_MUX___]+voiceAmpLfoLfmInput+lfm*LFM_INPUTS_NB,objects_first_output_id[LFO______]+voiceAmpLfo*MAX_OUTPUTS_PER_OBJ+LSIN);
            // config lfo modulation
            setLfmCoders(lfm,voiceAmpLfoLfmInput,255,120);
        }                           
        // connect lfo to voice2 freq
        //obj_connect(objects_first_input_id[VOICE____]+voice*VOICES_INPUTS_NB+VFRQ,objects_first_output_id[LFO______]+voice2ampLfo*MAX_OUTPUTS_PER_OBJ+LSIN);

        // connect lfo to lfm1:1
        obj_connect(objects_first_input_id[LF_MUX___]+voiceFreqLfoLfmInput+lfm1*LFM_INPUTS_NB,objects_first_output_id[LFO______]+voiceAmpLfo*MAX_OUTPUTS_PER_OBJ+LSIN);
        // connect lfm1 to voice2 freq
        obj_connect(objects_first_input_id[VOICE____]+voice*VOICES_INPUTS_NB+VFRQ,objects_first_output_id[LF_MUX___]+LMUXO*MAX_LFM+lfm1);
        // lfm1 in0 base wide open ; no input   
        setLfmCoders(lfm1,0,MAX_CTL_ATT-1,0);
        // lfm1 in1 att wide open
        setLfmCoders(lfm1,voiceFreqLfoLfmInput,0,MAX_CTL_ATT/2-1);
        // connect adsr to lfm1:2
        obj_connect(objects_first_input_id[LF_MUX___]+voiceFreqAdsrLfmInput+lfm1*LFM_INPUTS_NB,objects_first_output_id[ADSR_____]+adsr*MAX_OUTPUTS_PER_OBJ+ADSR_SHAPE);         
        // lfm1 in2 att wide open
        setLfmCoders(lfm1,voiceFreqAdsrLfmInput,0,MAX_CTL_ATT/2-1);
    }
    else{           
        if(voiceFreqLfo>=0){
            setLfosFrCoders(voiceFreqLfo,freqLfoFreq,0);
            setLfosFrequency(voiceFreqLfo);

            obj_connect(objects_first_input_id[VOICE____]+voice*VOICES_INPUTS_NB+VFRQ,objects_first_output_id[LFO______]+voiceFreqLfo*MAX_OUTPUTS_PER_OBJ+LTRI);
        }

        // CX (voice)SIP (adsr)
        printf("Amp ctl adsr:%u ",adsr);
        obj_connect(objects_first_input_id[VOICE____]+voice*VOICES_INPUTS_NB+VSPW+vwave,objects_first_output_id[ADSR_____]+adsr*MAX_OUTPUTS_PER_OBJ+ADSR_SHAPE);
        
        printf("voice:%u coderFreq:%i frAtt:%i wave:%u Ampl:%u amplAtt:%i\n\n",
            voice,voices[voice].coderFreq,voices[voice].coderFreqAtt,vwave,voices[voice].coderWaveAmpl[vwave],voices[voice].coderWaveAmplAtt[vwave]);
    }

}*/
// Ids of lfm inputs/outputs are read from the tables built at init,
// so the layout (input-major or lfm-major) is never assumed here.
static inline int16_t lfmInId(uint8_t lfm,uint8_t inp){return lfm_ctl_input_id[inp][lfm];}
static inline int16_t lfmOutId(uint8_t lfm){return lfm_ctl_output_id[LMUXO][lfm];}

void sound3(uint8_t voice,uint8_t vwave,uint16_t coderFreq,uint8_t coderAttF,uint8_t coderAmp,uint8_t coderAmpAtt,
        uint8_t coderCr,uint8_t crAtt,
        int8_t adsr,int8_t sourceAdsr,
        int8_t lfm,int8_t lfm1,uint8_t voiceAmpLfoLfmInput,uint8_t voiceFreqLfoLfmInput,uint8_t voiceFreqAdsrLfmInput,
        int8_t voiceAmpLfo,uint16_t ampLfoFreq,
        int8_t voiceFreqLfo,uint16_t freqLfoFreq
    )
{
    // config voice
    vsetup(voice,coderFreq,coderAttF,coderAmp,coderAmpAtt,coderCr,crAtt,vwave);

    // connect source to adsr (sourceAdsr<0 : lfo number -1-sourceAdsr ; otherwise touch button number)
    uint8_t sourceType=TBUTTON__;
    int8_t sourceNum=sourceAdsr;
    uint8_t waveF=0;
    if(sourceAdsr<0){
        sourceNum=-1-sourceAdsr;
        sourceType=LFO______;
        waveF=WSIN;
    }
    obj_connect(objects_first_input_id[ADSR_____]+adsr*ADSR_INPUTS_NB+STAR,
                objects_first_output_id[sourceType]+sourceNum*MAX_OUTPUTS_PER_OBJ+waveF);

    // config lfo amp
    if(voiceAmpLfo>=0){
        setLfosFrCoders(voiceAmpLfo,ampLfoFreq,0);
        setLfosFrequency(voiceAmpLfo);
    }

    if(lfm>=0){
        // connect lfm0 to voice:wave amp input
        obj_connect(objects_first_input_id[VOICE____]+voice*VOICES_INPUTS_NB+VSPW+vwave,lfmOutId(lfm));
        // connect adsr to lfm0:0 genAmp input
        uint8_t adsrLfmInput=0;
        setLfmCoders(lfm,adsrLfmInput,lfmCoder[adsrLfmInput][lfm],70);
        obj_connect(lfmInId(lfm,adsrLfmInput),objects_first_output_id[ADSR_____]+adsr*MAX_OUTPUTS_PER_OBJ+ADSR_SHAPE);
        if(voiceAmpLfo>=0){
            // connect lfo amp to lfm0:1
            obj_connect(lfmInId(lfm,voiceAmpLfoLfmInput),objects_first_output_id[LFO______]+voiceAmpLfo*MAX_OUTPUTS_PER_OBJ+LSIN);
            // config lfo modulation
            setLfmCoders(lfm,voiceAmpLfoLfmInput,255,120);
        }

        if(lfm1>=0){
            // connect lfm1 to voice freq
            obj_connect(objects_first_input_id[VOICE____]+voice*VOICES_INPUTS_NB+VFRQ,lfmOutId(lfm1));
            // lfm1 in0 base wide open ; no input
            setLfmCoders(lfm1,0,MAX_CTL_ATT-1,0);
            if(voiceAmpLfo>=0){
                // connect lfo to lfm1:voiceFreqLfoLfmInput (same lfo as amp)
                obj_connect(lfmInId(lfm1,voiceFreqLfoLfmInput),objects_first_output_id[LFO______]+voiceAmpLfo*MAX_OUTPUTS_PER_OBJ+LSIN);
                // lfm1 lfo input att wide open
                setLfmCoders(lfm1,voiceFreqLfoLfmInput,0,MAX_CTL_ATT/2-1);
            }
            // connect adsr to lfm1:voiceFreqAdsrLfmInput
            obj_connect(lfmInId(lfm1,voiceFreqAdsrLfmInput),objects_first_output_id[ADSR_____]+adsr*MAX_OUTPUTS_PER_OBJ+ADSR_SHAPE);
            // lfm1 adsr input att wide open
            setLfmCoders(lfm1,voiceFreqAdsrLfmInput,0,MAX_CTL_ATT/2-1);
        }
    }
    else{
        if(voiceFreqLfo>=0){
            setLfosFrCoders(voiceFreqLfo,freqLfoFreq,0);
            setLfosFrequency(voiceFreqLfo);
            obj_connect(objects_first_input_id[VOICE____]+voice*VOICES_INPUTS_NB+VFRQ,
                        objects_first_output_id[LFO______]+voiceFreqLfo*MAX_OUTPUTS_PER_OBJ+LTRI);
        }

        // CX (voice)SIP (adsr)
        printf("Amp ctl adsr:%u ",adsr);
        obj_connect(objects_first_input_id[VOICE____]+voice*VOICES_INPUTS_NB+VSPW+vwave,
                    objects_first_output_id[ADSR_____]+adsr*MAX_OUTPUTS_PER_OBJ+ADSR_SHAPE);

        printf("voice:%u coderFreq:%i frAtt:%i wave:%u Ampl:%u amplAtt:%i\n\n",
            voice,voices[voice].coderFreq,voices[voice].coderFreqAtt,vwave,voices[voice].coderWaveAmpl[vwave],voices[voice].coderWaveAmplAtt[vwave]);
    }
}

void testSetup()
{
    // 1096 110Hz // 1936 440Hz // 2355 880Hz    @420/octave demi_ton 35

    #define VOICE0 0x80
    #define VOICE1 0x40
    #define VOICE2 0x20
    #define VOICE3 0x10
    uint8_t action=VOICE3|VOICE0|VOICE1|VOICE2;

    printf("\n========== test-setup 0x%02X ==========\n",action);    

    if((action&VOICE0) != 0){  
        
        // config voice 0    
        uint8_t  voice=0;
        uint8_t  vwave=WTRI;
        uint16_t coderFreq=800; 
        uint16_t coderFil=1500;           
        uint8_t  coderAttF=125;
        uint8_t  coderAmp=15;
        uint8_t  coderAttAmp=80;         
        int8_t   cr=10;
        uint8_t  crAtt=64;
        uint8_t  freqLfo=0;
        uint16_t freqLfoCoder=3392;         // 5Hz  
        uint8_t  adsr=0;
        int8_t   adsrLfo=2;    
        uint32_t adsrLfoCoder=1384;         // 1384 6sec // 1790 3sec
        uint8_t  crLfo=4;
        uint16_t crLfoCoder=1820;
        uint8_t  lfm=0;
        uint8_t  lfmInp0=0;
        uint8_t  lfmInp1=1;
        uint8_t  lfmInp2=2;
        uint8_t  filtLfo=7; 

        voiceConfig(voice,vwave,coderFreq,coderAttF,freqLfo,freqLfoCoder,coderAmp,coderAttAmp,adsr,adsrLfo,adsrLfoCoder,cr,crAtt);
        
        asetup(adsr,6,28,12,96);

        voices[voice].coderCycleRAtt=200;                           // input atten
        sub_lfo(VOICE____,0,VOICES_INPUTS_NB,VCR_,4,1700,WTRI);                      // 1700=3.5s 

        setVoiceFilter(&voices[0],coderFil,100,255,0,255);
        //setLfosFreq(filtLfo,1790);
        //setLfosCycleR(filtLfo,0,0);

        //obj_connect(objects_first_input_id[VOICE____]+1*VOICES_INPUTS_NB+VFIL,objects_first_output_id[LFO______]+filtLfo*MAX_OUTPUTS_PER_OBJ+LTRI);        
        
        printf("\n");
    }

    // config voice 1
    if((action&VOICE1) != 0){
        
        uint8_t  v=1;
        uint8_t  filtLfo=6;
        uint8_t  adsr=1;
        //sound3(1,WSIN,1740,12,8,60,1,adsr,-3,0,0,0,0,0,-1,3499,1,3499);
        voiceConfig(v,WSIN,1740,12,1,3499,0,30,adsr,3,1200,1,0);
        setVoiceAmplCoders(&voices[v],5,30,0);
        asetup(adsr,96,60,11,90);

        setVoiceFilter(&voices[1],2000,100,150,0,255);
        
        setLfosFrCoders(filtLfo,1384,0);
        setLfosFrequency(filtLfo);
        setLfosCycleR(filtLfo,0,0);     //MAXCODER_RC/2,0);

        obj_connect(objects_first_input_id[VOICE____]+1*VOICES_INPUTS_NB+VFIL,objects_first_output_id[LFO______]+filtLfo*MAX_OUTPUTS_PER_OBJ+LTRI);
    }

    //  config voice2 (touch button 0)
    if((action&VOICE2) != 0){ 
        //      tb0->adsr->mux0inp0->sinpower2
        //      lfo5->mux1inp1 coder[1][1]=32563
        //      mux1->mux0inp1
        uint8_t adsr=2;
        sound3(2,WSIN,2300,70,0,30,1,0,adsr,0,0,1,1,1,2,5,3499,5,3499);
        asetup(adsr,127,32,16,127);
    }

    if((action&VOICE3) != 0){ 
        //      tb1->adsr->mux0inp0->sinpower2
        //      lfo5->mux1inp1 coder[1][1]=32563
        //      mux1->mux0inp1
        uint8_t adsr=3;
        uint8_t v=3;
        uint8_t b=1;
        uint16_t coderf=1500;
        vsetup(v,coderf,0,0,120,0,0,WSIN,coderf+409,0,MAX_CTL_ATT,0,0,1,0,63,0);
        obj_connect(objects_first_input_id[ADSR_____]+adsr*ADSR_INPUTS_NB+STAR,objects_first_output_id[TBUTTON__]+b*MAX_OUTPUTS_PER_OBJ+TOUCH_BUTTON);
        obj_connect(objects_first_input_id[VOICE____]+v*VOICES_INPUTS_NB+VSPW+WSIN,objects_first_output_id[ADSR_____]+adsr*MAX_OUTPUTS_PER_OBJ+ADSR_SHAPE);
        asetup(adsr,1,40,1,40);
        setEchoCoder(0,100,0,50,0,63,0);
    }

    if(action==0){
        uint8_t     voice=0;
        uint8_t     vwave=WSIN;
        int8_t      cr=0;
        uint8_t     crAtt=0;
        uint16_t    coderF=1943;
        uint8_t     coderAttF=70;
        uint8_t     coderA=10;
        uint8_t     coderAttA=0;
        uint8_t     lfo=0;
        uint8_t     lwave=WSIN;

        setLfosFrCoders(lfo,(uint16_t)1819,0); // 0.375hz ; 2239 0.75hz ; 2659 1.5hz ; 3079 3hz ; 3499 6hz
        setLfosFrequency(lfo);
        setLfosCycleR(lfo,0,0);

        vsetup(voice,coderF,coderAttF,coderA,coderAttA,cr,crAtt,vwave);

        setVoiceFilter(&voices[voice],coderF,100,255,0,255);
        //obj_connect(objects_first_input_id[VOICE____]+voice*VOICES_INPUTS_NB+VFIL,objects_first_output_id[LFO______]+lfo*MAX_OUTPUTS_PER_OBJ+lwave);

        //obj_connect(objects_first_input_id[VOICE____]+voice*VOICES_INPUTS_NB+VFRQ,objects_first_output_id[LFO______]+lfo*MAX_OUTPUTS_PER_OBJ+lwave);        
    }
 
    printf("========================================\n");

//pwm_timer_1khz_enable(false);while(1){}
//disp_lfm(255,"testSetup");
}


// **** test voice ****

        Voice*      testV;
        uint8_t     voice=0;
        uint8_t     wave=WSIN;
        
        uint16_t    coderF=calcCoderFreq(440);
        uint16_t    coderFAtt=0;        
        int8_t      cr=0;
        uint8_t     crAtt=0;

        int16_t     coderFilterF=coderF+409;
        int16_t     coderFilterFAtt=0;
        int16_t     coderFilterLevAtt=0;
        int16_t     coderFilterRes=0;
        int16_t     coderFilterResAtt=0;

        uint16_t    coderWA[BASIC_WAVES_NB];
        uint16_t    coderWAA[BASIC_WAVES_NB];

        uint16_t    coderGA=1;
        uint16_t    coderGAA=0;

void testVoiceInit(uint8_t vv,uint8_t w)
{
        voice=vv;
        testV=&voices[voice];                           
        wave=w;
        coderF=calcCoderFreq(440);
        coderFAtt=0;        
        cr=0;
        crAtt=0;

        coderFilterF=coderF+409;
        coderFilterFAtt=0;
        coderFilterLevAtt=MAX_CTL_ATT-1;
        coderFilterRes=252;
        coderFilterResAtt=0;

        for(uint8_t w=0;w<BASIC_WAVES_NB;w++){coderWA[w]=0;}
        coderWA[wave]=WAVE_LEVEL_STEPS/3;

        for(uint8_t w=0;w<BASIC_WAVES_NB;w++){coderWAA[w]=0;}
        coderWAA[wave]=0;

        coderGA=1;
        coderGAA=0;
        
        vsetup(voice,coderF,coderFAtt,coderWA[wave],coderWAA[wave],cr,crAtt,wave,coderFilterF,coderFilterFAtt,coderFilterLevAtt,coderFilterRes,coderFilterResAtt,coderGA,coderGAA,63,0); 
}

void testVoice()
{ 
            Voice* v=testV;

            fillVoices();

            if( 
                   v->coderFreq             != coderF
                || v->coderFreqAtt          != coderFAtt
                || v->coderCycleR           != cr
                || v->coderCycleRAtt        != crAtt
                || v->coderFilterFreq       !=coderFilterF
                || v->coderFilterFreqAtt    != coderFilterFAtt
                || v->coderFilterLevAtt     != coderFilterLevAtt
                || v->coderFilterRes        != coderFilterRes
                || v->coderFilterResAtt     != coderFilterResAtt
                || v->coderWaveAmpl[0]      != coderWA[0]
                || v->coderWaveAmpl[1]      != coderWA[1]
                || v->coderWaveAmpl[2]      != coderWA[2]
                || v->coderWaveAmpl[3]      != coderWA[3]
                || v->coderWaveAmpl[4]      != coderWA[4]
                || v->coderWaveAmpl[5]      != coderWA[5]
                || v->coderGenAmpl          != coderGA
                || v->coderGenAmplAtt       != coderGAA
            )
            {
                dumpVoices(v,voice);

                coderF                  = v->coderFreq;         
                coderFAtt               = v->coderFreqAtt;      
                cr                      = v->coderCycleR;
                crAtt                   = v->coderCycleRAtt;
                coderFilterF            = v->coderFilterFreq;
                coderFilterFAtt         = v->coderFilterFreqAtt;
                coderFilterLevAtt       = v->coderFilterLevAtt;
                coderFilterRes          = v->coderFilterRes;
                coderFilterResAtt       = v->coderFilterResAtt;
                coderWA[0]              = v->coderWaveAmpl[0];
                coderWA[1]              = v->coderWaveAmpl[1];
                coderWA[2]              = v->coderWaveAmpl[2];
                coderWA[3]              = v->coderWaveAmpl[3];
                coderWA[4]              = v->coderWaveAmpl[4];
                coderWA[5]              = v->coderWaveAmpl[5];
                coderGA                 = v->coderGenAmpl;
                coderGAA                = v->coderGenAmplAtt;
            }
}
