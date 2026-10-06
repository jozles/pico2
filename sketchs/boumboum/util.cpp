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
#include "util.h"
#include "std_utils.h"
#include "coder.h"
#include "frequences.h"
#include "leds.h"
#include "st7789.h"
#include "menus.h"
#include "input_tables_management.h"
#include "sound_level_management.h"
#include "miscControls.h"

#define SYSTICK_BASE 0xE000E010UL

#define SYST_CSR  (*(volatile uint32_t *)(SYSTICK_BASE + 0x00))
#define SYST_RVR  (*(volatile uint32_t *)(SYSTICK_BASE + 0x04))
#define SYST_CVR  (*(volatile uint32_t *)(SYSTICK_BASE + 0x08))
#define SYST_CALIB (*(volatile uint32_t *)(SYSTICK_BASE + 0x0C))

extern bool st_buffer_free,st_dma_free,st_dma_done_blank,st_sched_free;

// boumboum

const char objects_names[][OBJECTS_NAME_LEN]={         
    #define Z(name,text) text,
    #include "objects.def"   
    #undef Z   
};

int16_t objects_first_input_id[OBJECT_TYPES_NB];
int16_t objects_first_output_id[OBJECT_TYPES_NB];

static int st_dma_channel;
static int ws_dma_channel;

int32_t* i2s_dma_buffers[2];                // les 2 pointeurs sur les 2 buffers dma

int32_t i2s_buf0[SAMPLES_PER_BUFFER*2] __attribute__((aligned(32)));     // le buffer 0 (512*2*4 bytes = 4k)
int32_t i2s_buf1[SAMPLES_PER_BUFFER*2] __attribute__((aligned(32)));     // le buffer 1

extern struct Voice voices[];
//extern uint16_t amplLevel[];

extern int32_t  voicesScopeDataBuffer[];
extern int16_t  voice_first_input_id;

extern float    lfosFrequency[MAX_LFO];                   
extern uint16_t lfosCodersFreq[MAX_LFO];
extern int16_t  lfo_first_output_id;
extern int16_t  adsr_first_output_id;
extern int16_t  lfo_first_input_id;
extern int16_t  adsr_first_input_id;

extern uint16_t adsrCoderAtt[MAX_ADSR];
extern uint16_t adsrCoderDec[MAX_ADSR];
extern uint16_t adsrCoderSus[MAX_ADSR];
extern uint16_t adsrCoderRel[MAX_ADSR];
extern uint16_t adsrCoderLev[MAX_ADSR];

extern uint16_t lfmCoder[MAX_INPUTS_PER_OBJ][MAX_LFM];
extern uint16_t lfmCoderAtt[MAX_INPUTS_PER_OBJ][MAX_LFM];
extern int16_t  lfm_ctl_input_id[MAX_INPUTS_PER_OBJ][MAX_LFM];

extern int16_t  ctl_input_srce[MAX_INPUTS];
extern char     ctl_input_name[MAX_INPUTS][IN_OUT_NAME_LEN]; 
extern char     ctl_output_name[MAX_OUTPUTS][IN_OUT_NAME_LEN];

volatile uint32_t millisCounter;

#define R1 6
#define R2 8
#define MAXBLK 5

volatile uint32_t durOffOn[]={LEDOFFDUR,LEDONDUR/R1,LEDOFFDUR/R2,LEDONDUR/R1,LEDOFFDUR/R2,LEDONDUR/R1,LEDOFFDUR/R2,LEDONDUR/R1,LEDOFFDUR/R2,LEDONDUR/R1,LEDOFFDUR/R2,LEDONDUR/R1};

volatile uint8_t  led=0;
volatile uint32_t ledBlinker=0;

static repeating_timer millisTimer;

bool setupComplete=false;

//extern volatile bool codersTB[CODER_NB];

void blank(void* s,uint32_t len)
{
    blank_(s,len,0x00);
}

void system_error(const char* s,int32_t v)
{
    printf("syst err:%s :%i",s,v);
    tft_draw_text_12x12_dma_mult(0,0,s,0x001f,0,1);
    while(1){} 
}

void system_error(const char* s)
{
    system_error(s,0);
}

uint32_t signal_overflow(const char* s,uint16_t id,int32_t val,int32_t min,int32_t max)
{
    if(val>max || val<min){
        printf("signal ovf:%s (min:%u max:%u v:%u) id:%d\n",s,min,max,val,id);
        tft_draw_text_12x12_dma_mult(0,0,s,0x001f,0,2);
        char buf[TFT_W/12+1];
        int8_t l=convIntToString(buf,val);buf[l]=';';
        l+=convIntToString(buf+l+1,min);buf[l++]=':';
        l+=convIntToString(buf+l+1,max);buf[l++]='\0';
        tft_draw_text_12x12_dma_mult(0,27,buf,0x001f,0,1);
        if(val>max){val=max;}
        else val=min;
    }
    return val;
}

void __not_in_flash_func(blank32)(void *ptr, uint32_t len24)
{
    // longueur sur 24 bits
    len24 &= 0x00FFFFFF;

    uint32_t *p = (uint32_t *)ptr;
    uint32_t n32 = len24 >> 2;      // nombre de mots 32 bits
    uint32_t rem = len24 & 3;       // octets restants

    while (n32--) {
        *p++ = 0;
    }

    uint8_t *b = (uint8_t *)p;
    while (rem--) {
        *b++ = 0;
    }
}

uint pwm_irq_slice=PWM_IRQ_SLICE;

void __not_in_flash_func(pwm_irq_handler)() {

    //gpio_put(TST_PIN,HIGH);

    pwm_hw->intr = 1u << pwm_irq_slice; // pwm_clear_irq(pwm_irq_slice);   // slice 0 (clear irq)

    millisCounter++;

    coderTimerHandler();

    lfosHandler();

    adsrHandler();

    lfmScopeHandler();
}

void init_pwm_timer_1khz() {

    pwm_config cfg = pwm_get_default_config();

    // 150 MHz / 150 = 1 MHz → wrap = 1000 → 1 kHz
    pwm_config_set_clkdiv(&cfg,150.0f);
    pwm_config_set_wrap(&cfg, 1000);

    pwm_init(pwm_irq_slice, &cfg, false);

    pwm_clear_irq(pwm_irq_slice);
    pwm_set_irq_enabled(pwm_irq_slice, false);

    irq_set_exclusive_handler(PWM_IRQ_WRAP, pwm_irq_handler);

    irq_set_enabled(PWM_IRQ_WRAP, false);
}

void pwm_timer_1khz_enable(bool start_stop)
{
    if(start_stop){pwm_clear_irq(pwm_irq_slice);}    // vide le pending IRQ

    pwm_set_enabled(pwm_irq_slice, start_stop);      // start_stop PWM
    pwm_set_irq_enabled(pwm_irq_slice, start_stop);  // start_stop la source d’IRQ
    irq_set_enabled(PWM_IRQ_WRAP, start_stop);       // start_stop l’IRQ dans le NVIC

    if(!start_stop){
        pwm_clear_irq(pwm_irq_slice);    // vide le pending IRQ
        printf("1KHz PWM timer stopped\n");
    }
    else printf("1KHz PWM timer started\n");        

}

/*
static bool __not_in_flash_func(millisTimerHandler)(repeating_timer *t){
    millisCounter++;
    //coderTimerHandler();
//    if(millisCounter%1000==0){
//        tft_draw_int_12x12_dma_mult(165,12,0xffff,0x0000,1,millisCounter/1000);}
//        tft_draw_int_12x12_dma_mult(180,12,0xffff,0x0000,1,dma_tfr_count);}
    return true;
}
*/

void init_out_anal()
{
    gpio_set_function(GP_ANAL_PIN, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(GP_ANAL_PIN);

    pwm_set_wrap(slice, 255);       // 8 bits → ~500 kHz
    pwm_set_clkdiv(slice, 1.0f);    // fréquence max (à ajuster)
    pwm_set_enabled(slice, true);
}

void out_anal(uint32_t val_u32)
{
    int32_t  val = (int32_t)val_u32;                // valeur audio signée
    uint16_t pwm = (uint16_t)((val >> 24) + 128);   // Q1.31 -> 8 bits

    pwm_set_gpio_level(GP_ANAL_PIN, pwm);     
}


void quick_delay(uint32_t us){           // 0/1-> 2.33uS 5->8.33 10->14.25  env 1.2uS par step +2.25 init
    for(uint32_t i=0;i<us-1;i++){
        __asm volatile("nop");
    }
}

void delay_ms(uint32_t ms) {
    for(uint32_t i=0;i<ms;i++){quick_delay(1000);}
}

void delayBlk(uint8_t sec){
    for(uint8_t i=0;i<sec;i++){
    sleep_ms(950);gpio_put(LED,HIGH);sleep_ms(50);gpio_put(LED,LOW);}
}

#ifdef GLOBAL_DMA_IRQ_HANDLER

void __not_in_flash_func(global_dma_irq_handler)(){

    //gpio_put(TST_PIN,HIGH);    
    
    uint32_t global_dma_irq_status = dma_hw->intr;

    if((global_dma_irq_status & (1u << ws_dma_channel))!=0){
        ws_dma_irq_handler();
    } 
    if((global_dma_irq_status & (1u << st_dma_channel))!=0){
        st_dma_irq_handler();
    }
    
    //gpio_put(TST_PIN,LOW);     
}

void init_global_dma_irq(){
    irq_set_exclusive_handler(DMA_IRQ_1, global_dma_irq_handler);
    irq_set_enabled(DMA_IRQ_1, true);
}
#endif  // GLOBAL_DMA_IRQ_HANDLER

// ****** détection de gpio via irq et flag ******

bool gpio_irq_set=false;

void gpio_irq_handler(uint gpio, uint32_t events) {
    if (events & GPIO_IRQ_EDGE_RISE) {
        gpio_irq_set=true;
    }
}

void gpio_irq_init(uint pin) {
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    gpio_pull_up(pin);                     // si capteur open-collector

    gpio_set_irq_enabled_with_callback(
        pin,
        GPIO_IRQ_EDGE_RISE,                // front 
        true,
        gpio_irq_handler
    );
    gpio_irq_set=false;
}

// hardware full init 
void setup(){

    gpio_init(TST_PIN);gpio_set_dir(TST_PIN,GPIO_OUT); gpio_put(TST_PIN,LOW); 

    gpio_init(LED);gpio_set_dir(LED,GPIO_OUT); gpio_put(LED,LOW);
    delayBlk(3);               

    gpio_init(PIN_DCDC_PSM_CTRL);gpio_set_dir(PIN_DCDC_PSM_CTRL, GPIO_OUT);
    gpio_put(PIN_DCDC_PSM_CTRL, 1); // PWM mode for less Audio noise
    
    // ****** objects ******
    inputsInit();
    objects_table_init();

    // ****** coders ******
    coderInit(CODER_GPIO_CLOCK,CODER_GPIO_DATA,CODER_GPIO_SW,CODER_GPIO_VCC,CODER_PIO_SEL0,CODER_SEL_NB,CODER_NB,CODER_TIMER_POOLING_INTERVAL_MS,CODER_STROBE_NUMBER);
    coderSetup(nullptr,nullptr,nullptr,nullptr,0);

    // ****** ws2812 ******
    ws_dma_channel=ledsWs2812Setup(ws2812_pio,WS2812_LED_PIN);
    if(ws_dma_channel<0){LEDBLINK_ERROR_DMA}

    // ****** st7789 ******
    st_dma_channel=st7789_setup(ST7789_SPI_SPEED);
    if(st_dma_channel<0){LEDBLINK_ERROR_DMA}
    printf("ws dma ch:%i st dma ch:%i\n",ws_dma_channel,st_dma_channel);

    // ****** global irq (st+ws) ******
    init_global_dma_irq();

    // ****** button(s) ******
    irq_button_init(BUTTON_PIN);
    touch_button_init(TOUCH_PIN);

    // ****** sound ******
    sound_tables_init();

    float fr0=440;                  // initial frequency every voices
    uint8_t cga=1;                  // initial gain for genAmpl
    voicesInit(voices,fr0,cga);     // every waveAmpl = 0 ; every voices genAmpl = cga

    // ****** lfos ******
    lfosInit();

    // ****** adsrs ******
    fillDur();
    adsrInit();

    // ****** lmuxs *******
    lf_mixer_init();

    // ****** 1kHZ irq ******
    init_pwm_timer_1khz();      // init engine for millitimers+coders+lfos+adsr

    // ****** i2s ******
    i2s_dma_buffers[0]=i2s_buf0;
    i2s_dma_buffers[1]=i2s_buf1;
    i2sSetup(_i2s_pio,PICO_AUDIO_I2S_DATA_PIN,i2s_dma_buffers);     // start i2s engine

    voices[0].coderWaveAmpl[WSIN]=31;
    voices[0].coderWaveAmplAtt[WSIN]=0;
    setVoicesAmpl(0,WSIN);
    printf("demo sinus f:%f rc:%i ampl:%d\n",fr0,cga,voices[0].basicWaveAmpl[WSIN]);delay_ms(100);      
    
    // tous les genAmpl sont à cga ; toutes les wavesAmpl à 0 sauf voices[0] sinus
    fillVoices();               // après i2sSetup avant scope de démo ; i2S non démarré 

    //for(uint16_t b=0;b<1024;b++){printf("%u %i %X\n",b,i2s_buf0[b],(uint32_t)i2s_buf0[b]);}

    pwm_timer_1khz_enable(true);    // start millicounter, coders, buttons, lfos, adsr 

//dumpVoices(voices);
//dumpStr(voiceScopeBuffer,256);
//dumpStr(i2s_buf0,256);
    // ****** scope check ******
    //scope(voicesScopeDataBuffer,voices[0].frequency,14,true,true,0,0,true);    // scope mode_calcul
    scope(i2s_buf0,voices[0].frequency,14,true,true,0,0,0);     // scope mode_data

    // après démo mute voices[0] sinus
    voices[0].coderWaveAmpl[WSIN]=cga;
    setVoicesAmpl(0,WSIN);

    gpio_irq_set = false;               // wait touch button to start
    while(!gpio_irq_set){
        debug_ticker();
        ledblinkn(3);
    }
    gpio_irq_set=false;

    /*while(!codersTB[0][RISE]){
        debug_ticker();
        ledblinkn(3);
    }
    codersTB[0][RISE]=false;*/
    
    // ****** hello ******
    tft_fill_rect_blank(0,0,TFT_H,TFT_W);
    
    uint8_t m=3;
    tft_draw_text_12x12_dma_mult((TFT_W-(6*10*m))/2,(TFT_H-m*10)/2, "ST7789", 0xF80F, 0x0000,m); // ST7789

    uint8_t ls=16;
    char s[ls];memset(s,0x00,ls);
    int t=convIntToString(s,TFT_W);s[t]='x';convIntToString(s+t+1,TFT_H);

    tft_draw_text_12x12_dma_mult((TFT_W-(7*10))/2,TFT_H/2+14,s, 0xF81F, 0x0000,1); 

    const char* version=VERSION;
    tft_draw_text_12x12_dma_mult((TFT_W-(strlen(version)*10))/2,TFT_H/2+25,version, 0xFFE0, 0x0000,1);

    delayBlk(5);

    tft_fill_rect_blank(0,0,TFT_H,TFT_W);

    setupComplete=true;
    printf("end setup \n\n");
}

// ================= test Setup =====================

void obj_connect(int16_t inp_id,int16_t out_id)
{
    // make sure input is properly disconnected
    disconnect_input(inp_id,ctl_input_srce[inp_id]);

    //ctl_input_srce[inp_id]=out_id;
    // connect input on output via id chain
    connect_input(inp_id,out_id);

    char libi[IN_OUT_NAME_LEN+1];memset(libi,0x00,IN_OUT_NAME_LEN+1);
    memcpy(libi,&ctl_input_name[inp_id][0],IN_OUT_NAME_LEN);
    char libo[IN_OUT_NAME_LEN+1];memset(libo,0x00,IN_OUT_NAME_LEN+1);
    memcpy(libo,&ctl_output_name[ctl_input_srce[inp_id]],IN_OUT_NAME_LEN);    
    printf("%u:%s - %u:%s\n",inp_id,libi,out_id,libo); 
}

void sub_lfo(uint8_t object_type,uint8_t object_nb,uint8_t vInpNb,int8_t lfo,uint16_t coder,uint8_t lOutNb)
{
    uint8_t launcher;
    if(lfo>=0){
        launcher=LFO______;
        setLfosFreq(lfo,coder);
    }
    else {
        launcher=TBUTTON__;
        lfo=-lfo-1;    // numéro du touchb
        lOutNb=0;
    }
    
    // CX (voice)vInpNb (lfo out touche)lOutNb
    printf("(%i-%i-%i) %s:%u inp:%u lfo:%u outNb:%u f:%f",
        objects_first_input_id[VOICE____],objects_first_output_id[launcher],objects_first_output_id[ADSR_____],objects_names[object_type],object_nb,vInpNb,lfo,lOutNb,lfosFrequency[lfo]);
    obj_connect(objects_first_input_id[object_type]+object_nb*MAX_INPUTS_PER_OBJ+vInpNb,objects_first_output_id[launcher]+lfo*MAX_OUTPUTS_PER_OBJ+lOutNb);
}

void voiceConfig(uint8_t voice,uint8_t wave,uint16_t coderFreq,uint8_t attenFreqLevel,uint8_t freqLfo,uint16_t freqLfoCoder,
    uint8_t manualAmpLevel,uint8_t attenAmpLevel,uint8_t adsr,int8_t adsrLfo,uint16_t adsrLfoFreqCoder,int8_t rc)       // adsrLfo >=0 lfo ; <0 touch
{
    voices[voice].coderFreq=coderFreq;              
    float f=calcFreq(voices[voice].coderFreq);
    //voices[voice].basicFrequency=f;
    voices[voice].coderCycleR=rc+RC_TABLES_NB-1; 
    setVoiceFrequency(f,&voices[voice],rc);
  
    voices[voice].coderWaveAmpl[wave]=manualAmpLevel;          // manual level
    voices[voice].coderWaveAmplAtt[wave]=attenAmpLevel;        // input level
    setVoicesAmpl(voice,wave);
    setVoicesFreqAtt(voice,attenFreqLevel);

    if(freqLfo!=0){sub_lfo(VOICE____,voice,VFRQ,freqLfo,freqLfoCoder,WTRI);}    // modulation freq de la voix

    // CX (voice)SIP (adsr)
    printf("Amp ctl adsr:%u ",adsr);
    obj_connect(objects_first_input_id[VOICE____]+voice*MAX_INPUTS_PER_OBJ+VSPW+wave,objects_first_output_id[ADSR_____]+adsr*MAX_OUTPUTS_PER_OBJ+ADSR_SHAPE);

    sub_lfo(ADSR_____,adsr,STAR,adsrLfo,adsrLfoFreqCoder,WTRI);                 // adsr launcher lfo/touchB
    
    printf("voice:%u coderFreq:%i frAtt:%i wave:%u Ampl:%u amplAtt:%i\n\n",
        voice,voices[voice].coderFreq,voices[voice].coderFreqAtt,wave,voices[voice].coderWaveAmpl[wave],voices[voice].coderWaveAmplAtt[wave]);    
}

void vsetup(uint8_t v,uint16_t coderFreq,uint8_t attenFreqLevel,uint8_t manualAmpLevel,uint8_t attenAmpLevel,int8_t rc,uint8_t wave)
{
    voices[v].coderFreq=coderFreq;              
    float f=calcFreq(voices[v].coderFreq);
    //voices[v].basicFrequency=f;
    voices[v].coderCycleR=rc+RC_TABLES_NB-1; 
    setVoiceFrequency(f,&voices[v],rc);
    setVoicesFreqAtt(v,attenFreqLevel);     
  
    voices[v].coderWaveAmpl[wave]=manualAmpLevel;          // manual level
    voices[v].coderWaveAmplAtt[wave]=attenAmpLevel;        // input level
    setVoicesAmpl(v,wave);
   
}

void asetup(uint8_t adsr,uint8_t att,uint8_t dec, uint8_t sus,uint8_t rel){
        adsrCoderAtt[adsr]=att;setAdsrDur(adsr,ADSR_ATT,0);
        adsrCoderDec[adsr]=dec;setAdsrDur(adsr,ADSR_DEC,0);
        adsrCoderSus[adsr]=sus;setAdsrDur(adsr,ADSR_SUS,0);
        adsrCoderRel[adsr]=rel;setAdsrDur(adsr,ADSR_REL,0);
}

void sound3(uint8_t voice,uint8_t vwave,uint16_t coderFreq,uint8_t coderAttF,uint8_t coderAmp,uint8_t coderAmpAtt,uint8_t coderCr,
        uint8_t adsr,uint8_t sourceAdsr,
        uint8_t lfm,uint8_t lfm1,uint8_t voiceAmpLfoLfmInput,uint8_t voiceFreqLfoLfmInput,uint8_t voiceFreqAdsrLfmInput,
        uint8_t voiceAmpLfo,uint16_t ampLfoFreq,
        uint8_t voiceFreqLfo,uint16_t freqLfoFreq
    )
{
    // config voice
    vsetup(voice,coderFreq,coderAttF,coderAmp,coderAmpAtt,coderCr,vwave);                
            // connect source to adsr
    uint8_t srcAdsrOffset=0;
    uint8_t sourceType=TBUTTON__;
    uint8_t waveF=0;
    if(sourceAdsr<0){
       srcAdsrOffset=-1-sourceAdsr;
       sourceType=LFO______;
       waveF=WSIN; 
    }
    obj_connect(objects_first_input_id[ADSR_____]+adsr*MAX_INPUTS_PER_OBJ+STAR,objects_first_output_id[sourceType]+(sourceAdsr+srcAdsrOffset)*MAX_OUTPUTS_PER_OBJ+waveF);        

    // config lfo amp
    if(voiceAmpLfo>=0){setLfosFreq(voiceAmpLfo,ampLfoFreq);}
    
    if(lfm>=0){   
        // connect lfm0 to voice:wave amp input
        obj_connect(objects_first_input_id[VOICE____]+voice*MAX_INPUTS_PER_OBJ+VSPW+vwave,objects_first_output_id[LF_MUX___]+LMUXO*MAX_LFM+lfm);
        // connect adsr to lfm0:0 genAmp input
        uint8_t adsrLfmInput=0;
        setLfm(lfm,adsrLfmInput,lfmCoder[adsrLfmInput][lfm],70);   
        obj_connect(objects_first_input_id[LF_MUX___]+adsrLfmInput*MAX_LFM+lfm,objects_first_output_id[ADSR_____]+adsr*MAX_OUTPUTS_PER_OBJ+ADSR_SHAPE);
        if(voiceAmpLfo>=0){
            // connect lfo amp to lfm0:1
            obj_connect(objects_first_input_id[LF_MUX___]+voiceAmpLfoLfmInput*MAX_LFM+lfm,objects_first_output_id[LFO______]+voiceAmpLfo*MAX_OUTPUTS_PER_OBJ+LSIN);
            // config lfo modulation
            setLfm(lfm,voiceAmpLfoLfmInput,255,120);
        }                           
        // connect lfo to voice2 freq
        //obj_connect(objects_first_input_id[VOICE____]+voice*MAX_INPUTS_PER_OBJ+VFRQ,objects_first_output_id[LFO______]+voice2ampLfo*MAX_OUTPUTS_PER_OBJ+LSIN);

        // connect lfo to lfm1:1
        obj_connect(objects_first_input_id[LF_MUX___]+voiceFreqLfoLfmInput*MAX_LFM+lfm1,objects_first_output_id[LFO______]+voiceAmpLfo*MAX_OUTPUTS_PER_OBJ+LSIN);
        // connect lfm1 to voice2 freq
        obj_connect(objects_first_input_id[VOICE____]+voice*MAX_INPUTS_PER_OBJ+VFRQ,objects_first_output_id[LF_MUX___]+LMUXO*MAX_LFM+lfm1);
        // lfm1 in0 base wide open ; no input   
        setLfm(lfm1,0,MAX_CTL_ATT-1,0);
        // lfm1 in1 att wide open
        setLfm(lfm1,voiceFreqLfoLfmInput,0,MAX_CTL_ATT/2-1);
        // connect adsr to lfm1:2
        obj_connect(objects_first_input_id[LF_MUX___]+voiceFreqAdsrLfmInput*MAX_LFM+lfm1,objects_first_output_id[ADSR_____]+adsr*MAX_OUTPUTS_PER_OBJ+ADSR_SHAPE);         
        // lfm1 in2 att wide open
        setLfm(lfm1,voiceFreqAdsrLfmInput,0,MAX_CTL_ATT/2-1);
    }
    else{           
        if(voiceFreqLfo>=0){setLfosFreq(voiceFreqLfo,freqLfoFreq);}
        obj_connect(objects_first_input_id[VOICE____]+voice*MAX_INPUTS_PER_OBJ+VFRQ,objects_first_output_id[LFO______]+voiceFreqLfo*MAX_OUTPUTS_PER_OBJ+LTRI);

        // CX (voice)SIP (adsr)
        printf("Amp ctl adsr:%u ",adsr);
        obj_connect(objects_first_input_id[VOICE____]+voice*MAX_INPUTS_PER_OBJ+VSPW+vwave,objects_first_output_id[ADSR_____]+adsr*MAX_OUTPUTS_PER_OBJ+ADSR_SHAPE);
        
        printf("voice:%u coderFreq:%i frAtt:%i wave:%u Ampl:%u amplAtt:%i\n\n",
            voice,voices[voice].coderFreq,voices[voice].coderFreqAtt,vwave,voices[voice].coderWaveAmpl[vwave],voices[voice].coderWaveAmplAtt[vwave]);
    }
    //*/

    /*// lfm1 mux adsr + lfo to voice2 fr
    setLfm(lfm1,lfmInp0,MAX_CTL_ATT-1,0);                                    // in0 base wide open ; no input
    // connect adsr to lfm1:1
    obj_connect(objects_first_input_id[LF_MUX___]+lfmInp1*MAX_LFM+lfm1,objects_first_output_id[ADSR_____]+adsr*MAX_OUTPUTS_PER_OBJ+ADSR_SHAPE);
    setLfm(lfm1,lfmInp1,0,100);                                              // in1 adsr atten        
    // connect lfo to lfm1:2
    obj_connect(objects_first_input_id[LF_MUX___]+lfmInp2*MAX_LFM+lfm1,objects_first_output_id[LFO______]+voice2ampLfo*MAX_OUTPUTS_PER_OBJ+LSIN);
    setLfm(lfm1,lfmInp2,0,255);                                              // in2 lfo atten  
    // connect lfm1 to voice2 freq
    obj_connect(objects_first_input_id[VOICE____]+voice*MAX_INPUTS_PER_OBJ+VFRQ,objects_first_output_id[LF_MUX___]+LMUXO*MAX_LFM+lfm1);        
    //*/
}

void testSetup()
{
    // 1096 110Hz // 1936 440Hz // 2355 880Hz    @420/octave demi_ton 35

    #define VOICE0 0x80
    #define VOICE1 0x40
    #define VOICE2 0x20
    uint8_t action=VOICE0|VOICE1|VOICE2;

    printf("\n========== test-setup 0x%02X ==========\n",action);    

    if((action&VOICE0) != 0){  
        
        // config voice 0    
        uint8_t  voice=0;
        uint8_t  vwave=WSIN;
        uint16_t coderFreq=800;            
        uint8_t  coderAttF=12;
        uint8_t  coderA=8;
        uint8_t  coderAttA=20;         
        int8_t   cr=23;
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

        voiceConfig(voice,vwave,coderFreq,coderAttF,freqLfo,freqLfoCoder,coderA,coderAttA,adsr,adsrLfo,adsrLfoCoder,cr);
        asetup(adsr,6,28,12,96);

        voices[voice].coderCycleRAtt=60;                            // input atten
        sub_lfo(VOICE____,0,VCR_,4,1700,WTRI);                      // 1700=3.5s 
        printf("\n");
    }

    // config voice 1
    if((action&VOICE1) != 0){
        
        uint8_t filtLfo=6;
        uint8_t  adsr=1;
        //sound3(1,WSIN,1740,12,8,60,1,adsr,-3,0,0,0,0,0,-1,3499,1,3499);
        voiceConfig(1,WSIN,1740,12,1,3499,8,60,adsr,3,1790,1);
        asetup(adsr,40,28,12,96);

        setVoiceFilter(&voices[1],2000,100,255);
        setLfosFreq(filtLfo,1790);
        obj_connect(objects_first_input_id[VOICE____]+1*MAX_INPUTS_PER_OBJ+VFIL,objects_first_output_id[LFO______]+filtLfo*MAX_OUTPUTS_PER_OBJ+LTRI);
    }

    //  config voice2 (touch button 0)
    if((action&VOICE2) != 0){ 
        //      tb0->adsr->mux0inp0->sinpower2
        //      lfo5->mux1inp1 coder[1][1]=32563
        //      mux1->mux0inp1
        uint8_t adsr=2;
        sound3(2,WSIN,2300,70,4,30,1,adsr,0,0,1,1,1,2,5,3499,5,3499);
        asetup(adsr,127,32,16,127);
    }

    if(action==0){
        uint8_t     voice=0;
        uint8_t     vwave=WSIN;
        int8_t      rc=0;
        uint16_t    coderF=1700;
        uint8_t     coderAttF=70;
        uint8_t     coderA=20;
        uint8_t     coderAttA=0;
        uint8_t     lfo=0;
        uint8_t     lwave=WSIN;

        setLfosFreq(lfo,1819); // 0.375hz ; 2239 0.75hz ; 2659 1.5hz ; 3079 3hz ; 3499 6hz

        vsetup(voice,coderF,coderAttF,coderA,coderAttA,rc,vwave);

        obj_connect(objects_first_input_id[VOICE____]+voice*MAX_INPUTS_PER_OBJ+VFRQ,objects_first_output_id[LFO______]+lfo*MAX_OUTPUTS_PER_OBJ+lwave);        
    }

    printf("========================================\n");

//pwm_timer_1khz_enable(false);while(1){}
//disp_lfm(255,"testSetup");
}

// ******** diags/debug ********

void pd0(){
    printf("st_dma_chan=%d, st_dma_free=%d\n",st_dma_channel,get_st_dma_free());
    printf("ws_dma_chan=%d, ws_dma_done=%d\n",ws_dma_channel,get_ws_dma_done());
    printf("dma_irq_status %x\n",dma_hw->ints0);
}

void print_diag(char c,uint32_t gdis){
    printf("%c status IRQ:%d,%d\n",c,gdis&0x0000ffff,gdis>>16);
    pd0();
}

void print_diag(){
    print_diag(' ');
}

void print_diag(char c){
    printf("%c\n",c);
    pd0();
}


void show_cnt(uint32_t cnt,uint16_t x,uint16_t y,uint8_t mult){
    if(millisCounter%1000==0){
        int v10=(cnt%10);
        int v100=((cnt/10)%10);
        int v1000=((cnt/100)%10);
        int v10000=((cnt/1000)%10);
        int v100000=((cnt/10000)%10);
        char a[]={(char)(v100000+48),(char)(v10000+48),(char)(v1000+48),(char)(v100+48),(char)(v10+48),(char)0x00};
        tft_draw_text_12x12_dma_mult(x,y,a,0xffff,0x0000,mult);
    }
}

void show_cnt(uint32_t cnt,uint16_t x,uint16_t y){
    show_cnt(cnt,x,y,1);
}

// ***********  led  ***********

uint32_t last_c2=0;
uint32_t last_c3=0;
uint32_t c2 = dma_hw->ch[2].ctrl_trig;
uint32_t c3 = dma_hw->ch[3].ctrl_trig;
void ledblinkn(uint8_t n){
    if(
        (led==0 && (millisCounter-ledBlinker)>(durOffOn[led]-durOffOn[led+1]-(n-1)*(durOffOn[led+2]+durOffOn[led+3]))) 
        || 
        (led!=0 && (millisCounter-ledBlinker)>(durOffOn[led]))
    )
    {

if (c2 != last_c2 || c3 != last_c3) {
    printf("DMA2/3 MODIFIED: c2=%08x c3=%08x\n", c2, c3);
}
last_c2 = c2;
last_c3 = c3;

        if(n>MAXBLK){n=MAXBLK;}
        ledBlinker=millisCounter;
        if(led<((2*n)-1)){led++;}
        else led=0;
        gpio_put(LED,led&0x01);
    }
}
