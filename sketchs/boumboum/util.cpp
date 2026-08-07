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

static int st_dma_channel;
static int ws_dma_channel;

int32_t* i2s_dma_buffers[2];                // les 2 pointeurs sur les 2 buffers dma

int32_t i2s_buf0[SAMPLES_PER_BUFFER*2] __attribute__((aligned(32)));     // le buffer 0 (512*2*4 bytes = 4k)
int32_t i2s_buf1[SAMPLES_PER_BUFFER*2] __attribute__((aligned(32)));     // le buffer 1

extern struct Voice voices[];
//extern uint16_t amplLevel[];

extern int32_t voicesScopeDataBuffer[];
extern int16_t voice_first_input_id;

extern float      lfosFrequency[MAX_LFO];                   
extern uint16_t   lfosCodersFreq[MAX_LFO];
extern int16_t    lfo_first_output_id;
extern int16_t    adsr_first_output_id;
extern int16_t    lfo_first_input_id;
extern int16_t    adsr_first_input_id;

extern uint16_t adsrCoderAtt[MAX_ADSR];
extern uint16_t adsrCoderDec[MAX_ADSR];
extern uint16_t adsrCoderSus[MAX_ADSR];
extern uint16_t adsrCoderRel[MAX_ADSR];
extern uint16_t adsrCoderLev[MAX_ADSR];

extern int16_t    ctl_input_srce[MAX_INPUTS];
extern char       ctl_input_name[MAX_INPUTS][IN_OUT_NAME_LEN]; 
extern char       ctl_output_name[MAX_OUTPUTS][IN_OUT_NAME_LEN];

volatile uint32_t millisCounter;

#define R1 6
#define R2 8
#define MAXBLK 5
volatile uint32_t durOffOn[]={LEDOFFDUR,LEDONDUR/R1,LEDOFFDUR/R2,LEDONDUR/R1,LEDOFFDUR/R2,LEDONDUR/R1,LEDOFFDUR/R2,LEDONDUR/R1,LEDOFFDUR/R2,LEDONDUR/R1,LEDOFFDUR/R2,LEDONDUR/R1};
volatile uint8_t led=0;
volatile uint32_t ledBlinker=0;

static repeating_timer millisTimer;

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
    //gpio_put(TST_PIN,LOW);
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

bool gpio_irq_set=false;

uint32_t pin_irq_cnt=0;
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

    // ****** ws2812 ******
    ws_dma_channel=ledsWs2812Setup(ws2812_pio,WS2812_LED_PIN);
    if(ws_dma_channel<0){LEDBLINK_ERROR_DMA}

    // ****** st7789 ******
    st_dma_channel=st7789_setup(ST7789_SPI_SPEED);
    if(st_dma_channel<0){LEDBLINK_ERROR_DMA}
    printf("ws dma ch:%i st dma ch:%i\n",ws_dma_channel,st_dma_channel);

    // ****** global irq (st+ws) ******
    init_global_dma_irq();

    // ****** button ******
    gpio_init(BUTTON_PIN);gpio_set_dir(BUTTON_PIN,GPIO_IN);
    gpio_init(BUT_VCC_PIN);gpio_set_dir(BUT_VCC_PIN,GPIO_OUT); gpio_put(BUT_VCC_PIN,LOW);sleep_ms(100);gpio_put(BUT_VCC_PIN,HIGH);
    gpio_irq_init(BUTTON_PIN);  // après  init_global_dma_irq();

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

//pwm_timer_1khz_enable(false);testSetup();

    // après démo mute voices[0] sinus
    voices[0].coderWaveAmpl[WSIN]=cga;
    setVoicesAmpl(0,WSIN);

    gpio_irq_set = false;
    while(!gpio_irq_set){
        debug_ticker();
        ledblinkn(3);
    }
    gpio_irq_set=false;

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

    printf("end setup \n\n");

//print_diag();
}

void sub_test(int16_t inp,int16_t out)
{
    disconnect_input(inp,ctl_input_srce[inp]);
    ctl_input_srce[inp]=out;
    connect_input(inp,ctl_input_srce[inp]);
    char libi[IN_OUT_NAME_LEN+1];memset(libi,0x00,IN_OUT_NAME_LEN+1);
    memcpy(libi,&ctl_input_name[inp][0],IN_OUT_NAME_LEN);
    char libo[IN_OUT_NAME_LEN+1];memset(libo,0x00,IN_OUT_NAME_LEN+1);
    memcpy(libo,&ctl_output_name[ctl_input_srce[inp]],IN_OUT_NAME_LEN);    
    printf("%u:%s - %u:%s\n",inp,libi,out,libo); 
}

void voiceConfig(uint8_t voice,uint8_t wave,uint16_t coderFreq,uint8_t attenFreqLevel,uint8_t freqLfo,uint16_t freqLfoCoder,uint8_t manualAmpLevel,uint8_t attenAmpLevel,uint8_t adsr,uint8_t lfoAdsr,uint16_t lfoAdsrFreqCoder,int8_t rc)
{

    voices[voice].coderFreq=coderFreq;              
    float f=calcFreq(voices[voice].coderFreq);
    voices[voice].basicFrequency=f;  
    setVoiceFrequency(f,&voices[voice],rc);
  
    voices[voice].coderWaveAmpl[wave]=manualAmpLevel;          // manual level
    voices[voice].coderWaveAmplAtt[wave]=attenAmpLevel;        // input level
    setVoicesAmpl(voice,wave);
    setVoicesFreqAtt(voice,attenFreqLevel);

    // set freqLfo freq
    setLfosFreq(freqLfo,freqLfoCoder);    
    // CX (voice)FRE (freqLfo)TRI
    printf("(%i-%i-%i) Fr ctl voice:%u lfo:%u f:%f",voice_first_input_id,lfo_first_output_id,adsr_first_output_id,voice,freqLfo,lfosFrequency[freqLfo]);
    sub_test(voice_first_input_id+voice*MAX_INPUTS_PER_OBJ+VFRQ,lfo_first_output_id+freqLfo*MAX_OUTPUTS_PER_OBJ+WTRI);

    // CX (voice)SIP (adsr)
    printf("Amp ctl adsr:%u ",adsr);
    sub_test(voice_first_input_id+voice*MAX_INPUTS_PER_OBJ+VSPW,adsr_first_output_id+adsr);
    
    // set adsrLfo freq
    printf("(%i-%i) adsr:%u ",adsr_first_input_id,lfo_first_output_id,adsr);    
    setLfosFreq(lfoAdsr,lfoAdsrFreqCoder);
    // CX (adsr)STA (adsrLfo)SAW                        start adsr last
    sub_test(adsr_first_input_id+adsr*MAX_INPUTS_PER_OBJ+STAR,lfo_first_output_id+lfoAdsr*MAX_OUTPUTS_PER_OBJ+WSAW);       

    printf("voice:%u freq:%f coderFreq:%i frAtt:%i wave:%u Ampl:%u amplAtt:%i\n\n",voice,voices[voice].basicFrequency,voices[voice].coderFreq,voices[voice].coderFreqAtt,wave,voices[voice].coderWaveAmpl[wave],voices[voice].coderWaveAmplAtt[wave]);    
}

void testSetup()
{
    printf("\n>>test-setup\n");

    // config voice 0    
    uint8_t  voice=0;
    uint8_t  wave=WSIN;
    uint16_t coderFreq=800;            // 1096 110Hz // 1936 440Hz // 2355 880Hz    @420/octave demi_ton 35
    int8_t   rc=23;
    uint8_t  freqLfo=0;
    uint16_t freqLfoCoder=3392;         // 5Hz
    uint8_t  attenFreqLevel=12;
    uint8_t  manualAmpLevel=8;    
    uint8_t  adsr=0;
    uint8_t  adsrLfo=2;    
    uint8_t  attenAmpLevel=130;
    uint32_t adsrLfoCoder=1384;         // 6sec // 1790 3sec 

    voiceConfig(voice,wave,coderFreq,attenFreqLevel,freqLfo,freqLfoCoder,manualAmpLevel,attenAmpLevel,adsr,adsrLfo,adsrLfoCoder,rc);
    // CX (voice)SQP (adsr) 
    adsrCoderAtt[adsr]=6;setAdsrDur(adsr,ADSR_ATT,0);
    adsrCoderDec[adsr]=28;setAdsrDur(adsr,ADSR_DEC,0);
    adsrCoderSus[adsr]=12;setAdsrDur(adsr,ADSR_SUS,0);
    adsrCoderRel[adsr]=96;setAdsrDur(adsr,ADSR_REL,0); 
    printf("Amp ctl adsr:%u ",adsr);
    sub_test(voice_first_input_id+voice*MAX_INPUTS_PER_OBJ+VQPW,adsr_first_output_id+adsr);
    wave=WSQR;
    voices[voice].coderWaveAmpl[wave]=manualAmpLevel;          // manual level
    voices[voice].coderWaveAmplAtt[wave]=attenAmpLevel;        // input level 
    printf("\n");         

    // config voice 1    
    voice=1;
    wave=WSIN;
    coderFreq=1740;            // 1936 440Hz // 2355 880Hz 
    rc=1;   
    freqLfo=1;
    freqLfoCoder=3499;         // 6Hz
    attenFreqLevel=12;
    manualAmpLevel=8;    
    adsr=1;
    adsrLfo=3;    
    attenAmpLevel=70;
    adsrLfoCoder=1790;         // 6sec // 1790 3sec 

    voiceConfig(voice,wave,coderFreq,attenFreqLevel,freqLfo,freqLfoCoder,manualAmpLevel,attenAmpLevel,adsr,adsrLfo,adsrLfoCoder,rc); 
    adsrCoderAtt[adsr]=40;setAdsrDur(adsr,ADSR_ATT,0);
    adsrCoderDec[adsr]=28;setAdsrDur(adsr,ADSR_DEC,0);
    adsrCoderSus[adsr]=12;setAdsrDur(adsr,ADSR_SUS,0);
    adsrCoderRel[adsr]=96;setAdsrDur(adsr,ADSR_REL,0);    
//*/
//pwm_timer_1khz_enable(false);while(1){}
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

void dumpVal(uint32_t val){
    
    for(int i=0;i<4;i++){
        uint8_t v0=val>>(i*8)&0xff;
        printf("%02x",v0);
    }
    printf("\n");
}

void dumpStr16(int32_t* str){
    printf("%p    ",str);
    for(uint32_t i=0;i<16;i++){
        printf("%08x ",str[i]);
    }
    printf(" ");
    for(uint32_t i=0;i<16;i++){
        for(uint8_t j=0;j<4;j++){
            uint8_t v0=(str[i]>>((3-j)*8))&0x000000FF;
            if(v0>=0x20 && v0<0x7f){printf("%c",v0);}
            else{printf(".");}
        }
        printf(" ");
    }
    printf("\n");
}

void dumpStr(int32_t* str,uint32_t nb){
    for(uint32_t i=0;i<nb;i+=16){
        dumpStr16(&str[i]);
    }
    printf("\n");
}

// ******** unused ********

void pio_full_reset(PIO pio) {

    printf("pio#%p full reset\n",pio);

    // 1. Désactiver toutes les SM
    for (int sm = 0; sm < 4; sm++) {
        pio_sm_set_enabled(pio, sm, false);
    }

    // 2. Libérer toutes les SM dans le SDK
    for (int sm = 0; sm < 4; sm++) {
        pio_sm_unclaim(pio, sm);
    }

    // 3. Effacer la mémoire d’instructions PIO
    pio_clear_instruction_memory(pio);

    // 4. Redémarrer les diviseurs de clock
    for (int sm = 0; sm < 4; sm++) {
        pio_sm_clkdiv_restart(pio, sm);
    }

    // 5. Reset interne complet des SM
    for (int sm = 0; sm < 4; sm++) {
        pio_sm_restart(pio, sm);
    }

}

// ----------------------------------
//             conversions
// ******** from shutil2.cpp ********
// ----------------------------------

const char* chexa="0123456789ABCDEFabcdef\0";

const char* table64="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

uint16_t ato64(char* srce,char* dest,uint32_t len)   // len dest >= len srce*(1,33)+1
{
  uint8_t a;
  uint32_t i=0;
  uint32_t j=0;
  uint8_t k=len-((len/3)*3);

  for(i=0;i<len-k;i+=3){
    a=srce[i]>>2;dest[j]=table64[a];
    a=(srce[i]&0x03)<<4;a+=srce[i+1]>>4;dest[j+1]=table64[a];
    a=(srce[i+1]&0x0F)<<2;a+=srce[i+2]>>6;dest[j+2]=table64[a];
    a=srce[i+2]&0x3F;dest[j+3]=table64[a];
    j+=4;}

  if(k==1){dest[j]=table64[srce[i]>>2];dest[j+1]=table64[(srce[i]&0x03)<<4];return j+1;}
  if(k==2){
    a=srce[i]>>2;dest[j]=table64[a];
    a=(srce[i]&0x03)<<4;a+=srce[i+1]>>4;dest[j+1]=table64[a];
    a=(srce[i+1]&0x0F)<<2;dest[j+2]=table64[a];
    return j+2;}

  return j-1;
}

int convIntToString(char* str,int32_t num,uint8_t len)
{
  int i=0,t=0,num0=num;
  if(num<0){i=1;str[0]='-';}
  while(num0!=0){num0/=10;i++;}             // comptage nbre chiffres partie entière
  if(len!=0){i=len;}                        // len!=0 complète avec des 0 ou troncate
  t=i;
  for (i=i;i>0;i--){num0=num%10;num/=10;str[i-1]=chexa[num0];}
  str[t]='\0';
  if(str[0]=='\0'){str[0]='0';}
  return t;
}

int convIntToString(char* str,int32_t num)
{
  return convIntToString(str,num,0);
}

int convNumToString(char* str,float num)    // retour string terminée par '\0' ; return longueur totale '\0' inclus
{
  int i=0,v=0,t=0;

  t=convIntToString(str,(int)num);          // conv -> nbre chiffres partie entière

  num=num-(int)num;                         // décimales
  str[t]='.';
  for(i=0;i<2;i++){num=num*10;v=(int)num;num=num-v;str[t+1+i]=chexa[v];}
  t+=3;
  str[t]='\0';

  return t;
}

uint8_t conv_atob(const char* ascii,uint16_t* bin,uint8_t len)
{
  uint8_t j=0;
  uint8_t c;
  *bin=0;
  for(j=0;j<len;j++){c=ascii[j];if(c>='0' && c<='9'){*bin=*bin*10+c-48;}else break;}
  return j;
}

uint8_t conv_atobl(const char* ascii,uint32_t* bin,uint8_t len)
{
  uint8_t j=0;
  uint8_t c;
  *bin=0;
  for(j=0;j<len;j++){c=ascii[j];if(c>='0' && c<='9'){*bin=*bin*10+c-48;}else break;}
  return j;
}

#define TEXTIPADDRLENGTH 15

void charIp(char* aipadr,char* nipadr,char* jsbuf)
{
  char buf[TEXTIPADDRLENGTH+1];
  memset(buf,0x00,TEXTIPADDRLENGTH+1);
  for(int i=0;i<4;i++){
    sprintf(buf+strlen(buf),"%d",nipadr[i]);if(i<3){strcat(buf,".");}
  }
  memcpy(aipadr,buf,TEXTIPADDRLENGTH);
  if(jsbuf!=nullptr){strcat(jsbuf,buf);strcat(jsbuf,";");}
}

void charIp(char* aipadr,char* nipadr)
{
  charIp(aipadr,nipadr,nullptr);
}

void conv_atoh(char* ascii,uint8_t* h)
{
    uint8_t c=0;
  c = (uint8_t)(strchr(chexa,ascii[0])-chexa)<<4 ;
  c |= (uint8_t)(strchr(chexa,ascii[1])-chexa) ;
  *h=c;
}

void conv_htoa(char* ascii,uint8_t* h)
{
    uint8_t c=*h,d=c>>4,e=c&0x0f;
        ascii[0]=chexa[d];ascii[1]=chexa[e];
}

void conv_htoa(char* ascii,uint8_t* h,uint8_t len)
{
    for(uint8_t i=0;i<len;i++){
      conv_htoa(ascii+2*(len-i-1),(uint8_t*)(h+i));
    }
}

uint32_t convStrToHex(char* str,uint8_t len)
{
  uint8_t v0=0;
  uint32_t v=0;
  int i=0;

  char cc[2];cc[1]='\0';
  for(i=len-1;i>=0;i--){
    cc[0]=str[i];
    v0=strstr(chexa,cc)-chexa;if(v0>15){v0-=6;}
    v+=v0;
    v=v<<4;
  }
  return v;
}

float convStrToNum(char* str,int* sizeRead)
{
  float v=0;
  uint8_t v0=0;
  float pd=1;
  int minu=1;
  int i=0;
#define MAXL 10

  for(i=0;i<MAXL;i++){
    if(i==0 && str[i]=='+'){i++;}
    if(i==0 && str[i]=='-'){i++;minu=-1;}
    if(str[i]=='.'){if(pd==1){pd=10;}i++;}
    *sizeRead=i+1;
    if(str[i]!='_' && str[i]!='\0' && str[i]>='0' && str[i]<='9'){
      v0=*(str+i)-48;
      if(pd==1){v=v*10+v0;}
      else{v+=(float)v0/pd;pd*=10;}
    }
    else {i=MAXL;}
  }
  return v*minu;
}

int32_t convStrToInt(char* str,int* sizeRead)
{
  int32_t v=0;
  int minu=1;

#define MAXLS 12 // max int32 length -4 294 967 296 (+séparator)

  for(int i=0;i<MAXLS;i++){
    if(i==0){
        if(str[i]=='+'){i++;}
        else if(str[i]=='-'){i++;minu=-1;}
    }
    *sizeRead=i+1;

    if(str[i]!='_' && str[i]!='\0' && str[i]>='0' && str[i]<='9'){
      v*=10;
      v+=str[i]-'0';
    }
    else {i=MAXLS;}
  }
  return v*minu;
}

void dumpStr0(char* data,uint8_t len,bool cr)
{
    char a[]={0x00,0x00,0x00};
    uint8_t c;
    printf("   %x   ",(long)data);
    for(int k=0;k<len;k++){printf("%02x ",data[k]);}   //conv_htoa(a,(uint8_t*)&data[k]);printf(" %c",a);}
    printf("    ");
    for(int k=0;k<len;k++){
            c=data[k];
            if(c<32 || c>127){c='.';}
            printf("%c",(char)c);
    }
    if(cr){printf("\n");}
}

void dumpStr(char* data,uint32_t len,bool cr)
{
    while(len>=16){len-=16;dumpStr0(data,16,len>0);data+=16;}
    if(len!=0){dumpStr0(data,len,false);}
    if(cr){printf("\n");}
}

void dumpStr(char* data,uint32_t len)
{
  return dumpStr(data,len,true);
}

void dumpfield(char* fd,uint8_t ll)
{
    uint8_t a;
    for(int ff=ll-1;ff>=0;ff--){
            a=((fd[ff]&0xF0)>>4)+'0';if(a>'9'){a+=7;}printf("%c",(char)a);
            a=(fd[ff]&0x0F)+'0';if(a>'9'){a+=7;}printf("%c ",(char)a);
    }
    printf(" ");
}

uint8_t calcCrc(char* buf,int len)
{
  uint8_t crc=0,j,k,m;
  int i;

  for(i=0;i<len;i++){
    m=(uint8_t)buf[i];
    for(j=0;j<8;j++){
        k=(crc^m)&0x01;
        crc=crc>>1;
        if(k==1){crc=crc^0x8C;}     // 0x8C is 00011001 rigth rotated polynom
        m=m>>1;
    }
  }
  return crc;
}

uint8_t setcrc(char* buf,int len)
{
  uint8_t c=calcCrc(buf,len);
  conv_htoa(buf+len,&c);buf[len+2]='\0';
  return c;
}


void packDate(char* dateout,char* datein)
{
    for(int i=0;i<6;i++){
        dateout[i]=datein[i*2] << 4 | (datein[i*2+1] & 0x0F);
    }
}

void unpackDate(char* dateout,char* datein)
{
    for(int i=0;i<6;i++){
        dateout[i*2]=(datein[i] >> 4)+48; dateout[i*2+1]=(datein[i] & 0x0F)+48;
    }
}

void unpack(char* out,char* in,uint8_t len)
{
    for(int i=0;i<len;i++){
        in[i*2]=(out[i] >> 4)+48; in[i*2+1]=(out[i] & 0x0F)+48;
    }
}

void pack(char* out,char* in,uint8_t inputLen,bool rev)
{
  if(!rev){
    for(int i=0;i<inputLen;i+=2){
        in[i/2]=((out[i]-48)<<4)+(out[i+1]-48);
    }
  }
  else {
    for(int i=inputLen;i>0;i-=2){
        in[i/2]=((out[i]-48)<<4)+(out[i+1]-48);
    }
  }
}

uint16_t packHexa(const char* out,uint8_t len)
{
  uint16_t v=0;
  char cc[2];cc[1]='\0';
  for(uint8_t i=0;i<len;i+=2){
   cc[0]=*(out+i);
   v<<=4;
   v+=strstr(chexa,cc)-chexa;
   cc[0]=*(out+i+1);
   v<<=4;
   v+=strstr(chexa,cc)-chexa;
  }
  return v;
}

void unpackHexa(uint16_t out,char* in,uint8_t len)      // len =2 ou 4 !
{
  for(int8_t i=len;i>0;i-=2){
    
    in[i-1]=chexa[out&0x0f];
    out>>=4;
    in[i-2]=chexa[out&0x0f];
    out>>=4;
  }
}


uint8_t dcb2b(uint8_t val)
{
    return ((val>>4)&0x0f)*10+(val&0x0f);
}

uint32_t cvds(char* d14,uint8_t skip)   // conversion date packée (yyyymmddhhmmss 7 car) en sec
{
    uint32_t secDay=24*3600L;
    uint32_t secYear=365*secDay;
    uint32_t m28=secDay*28L,m30=m28+secDay+secDay,m31=m30+secDay;
    uint32_t monthSec[]={0,m31,monthSec[1]+m28,monthSec[2]+m31,monthSec[3]+m30,monthSec[4]+m31,monthSec[5]+m30,monthSec[6]+m31,monthSec[7]+m31,monthSec[8]+m30,monthSec[9]+m31,monthSec[10]+m30,monthSec[11]+m31};

    uint32_t aa=0;if(skip==0){aa=dcb2b(d14[0])*100L;};aa+=dcb2b(d14[1-skip]);
    uint32_t njb=aa/4L;       // nbre années bisextiles depuis année 0
    uint8_t  mm=dcb2b(d14[2-skip]);
                        if(mm>2 && aa%4==0){njb++;}
    uint32_t bisext=njb*secDay;

    return bisext+aa*secYear+monthSec[(mm-1)]+(dcb2b(d14[3-skip])-1)*secDay
            +dcb2b(d14[4-skip])*3600L+dcb2b(d14[5-skip])*60L+dcb2b(d14[6-skip]);
}

int  dateCmp(char* olddate,char* newdate,uint32_t offset,uint8_t skip1,uint8_t skip2)
{

    uint32_t oldds=cvds(olddate,skip1),newds=cvds(newdate,skip2);

    if((oldds+offset)<newds){return -1;}
    if((oldds+offset)>newds){return 1;}
    return 0;
}

void serialPrintDate(char* datein)
{
    for(int i=0;i<6;i++){
        printf("%c%c\n",(char)((datein[i] >> 4)+48),(char)((datein[i] & 0x0F)+48));}
}


bool ctlto(unsigned long time,uint16_t to)
{
    //Serial.print("ctlto=");Serial.print(time);Serial.print(" to=");Serial.println(to);
 return (millisCounter-time)>((uint32_t)to*1000);
}

void startto(unsigned long* time,uint16_t* to,uint16_t valto)
{
  *to=valto;
  *time=millisCounter;
        //Serial.print("startto=");Serial.print(*time);Serial.print(" to=");Serial.print(*to);Serial.print(" valto=");Serial.println(valto);
}

char getCh()
{
    char c='\0';
    c=getchar();
    printf("%c",(char)c);sleep_us(200);
    return c;
}

uint8_t getNumCh(char min,char max)
{
    char c=getchar();   
    while(c>max && c<min){
        c=getchar();}
    c-=48;return c;
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


extern char __bss_end__;   // Fin des statiques (DATA+BSS)

static inline uint32_t get_msp(void) {
    uint32_t msp;
    __asm volatile ("mrs %0, msp" : "=r" (msp));
    return msp;
}

void print_memory_report(void) {

    const uint32_t RAM_START = 0x20000000u;
    const uint32_t RAM_END   = 0x20080000u;   // 512 Ko

    uint32_t bss_end   = (uint32_t)&__bss_end__;
    uint32_t heap_curr = (uint32_t)sbrk(0);
    uint32_t msp       = get_msp();

    struct mallinfo mi = mallinfo();

    uint32_t ram_total      = RAM_END - RAM_START;
    uint32_t ram_static     = bss_end - RAM_START;
    uint32_t ram_heap_used  = (heap_curr > bss_end) ? (heap_curr - bss_end) : 0;
    uint32_t ram_stack_used = (RAM_END > msp) ? (RAM_END - msp) : 0;
    uint32_t ram_free       = (msp > heap_curr) ? (msp - heap_curr) : 0;

    printf("\n========== RAPPORT MEMOIRE ==========\n");

    printf("RAM totale          : %u bytes\n", ram_total);
    printf("Statiques (DATA+BSS): %u bytes\n", ram_static);

    printf("\n--- Heap ---\n");
    printf("Heap courant (sbrk) : 0x%08X\n", heap_curr);
    printf("Heap utilisé (approx): %u bytes\n", ram_heap_used);
    printf("mallinfo.uordblks   : %d bytes\n", mi.uordblks);
    printf("mallinfo.fordblks   : %d bytes\n", mi.fordblks);

    printf("\n--- Stack ---\n");
    printf("MSP                 : 0x%08X\n", msp);
    printf("Stack utilisée      : %u bytes\n", ram_stack_used);

    printf("\n--- RAM libre ---\n");
    printf("RAM libre (heap→stack): %u bytes\n", ram_free);

    printf("=====================================\n\n");
}
