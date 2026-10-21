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

extern int16_t objects_first_input_id[OBJECT_TYPES_NB];
extern int16_t objects_first_output_id[OBJECT_TYPES_NB];

static int st_dma_channel;
static int ws_dma_channel;

int32_t* i2s_dma_buffers[2];                // les 2 pointeurs sur les 2 buffers dma

int32_t i2s_buf0[SAMPLES_PER_BUFFER*2] __attribute__((aligned(32)));     // le buffer 0 (512*2*4 bytes = 4k)
int32_t i2s_buf1[SAMPLES_PER_BUFFER*2] __attribute__((aligned(32)));     // le buffer 1

extern struct Voice voices[];

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

extern uint16_t lfmCoder[LFM_INPUTS_NB][MAX_LFM];
extern uint16_t lfmCoderAtt[LFM_INPUTS_NB][MAX_LFM];
extern int16_t  lfm_ctl_input_id[LFM_INPUTS_NB][MAX_LFM];

extern int16_t  ctl_input_srce[LFM_INPUTS_NB];
//extern char     ctl_input_name[TOTAL_INPUTS_ID_REAL][IN_OUT_NAME_LEN]; 
//extern char     ctl_output_name[MAX_OUTPUTS][IN_OUT_NAME_LEN];

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

    ampGlideTick();                         // every timer tick, whatever the polling interval

    filterGlideTick();
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
    coderInit(CODER_GPIO_CLOCK,CODER_GPIO_DATA,CODER_GPIO_SW,CODER_GPIO_VCC,CODER_PIO_SEL0,CODER_SEL_NB,CODER_NB,CODER_TIMER_POLLING_INTERVAL_MS,CODER_STROBE_NUMBER);
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
    voicesInit(voices,fr0,cga);     // every waveAmpl = 0 ; every voice's genAmpl = cga

    // ****** lfos ******
    lfosInit();

    // ****** adsrs ******
    fillDur();
    adsrInit();

    // ****** lmuxs *******
    lf_mixer_init();

    // ****** 1kHZ irq ******
    init_pwm_timer_1khz();          // init engine for millitimers+coders+lfos+adsr

    // ****** i2s ******
    i2s_dma_buffers[0]=i2s_buf0;
    i2s_dma_buffers[1]=i2s_buf1;
    i2sSetup(_i2s_pio,PICO_AUDIO_I2S_DATA_PIN,i2s_dma_buffers);     // start i2s engine

    setVoiceAmplCoders(&voices[0],WSIN,WAVE_LEVEL_STEPS-1,0);
    printf("demo sinus f:%f rc:%i ampl:%d\n",fr0,cga,voices[0].basicWaveAmpl[WSIN]);delay_ms(100);      
    
    // tous les genAmpl sont à cga ; toutes les wavesAmpl à 0 sauf voices[0] sinus
    fillVoices();                   // après i2sSetup avant scope de démo ; i2S non démarré 

    pwm_timer_1khz_enable(true);    // start millicounter, coders, buttons, lfos, adsr 

    // ****** scope check ******
    //scope(voicesScopeDataBuffer,voices[0].frequency,14,true,true,0,0,true);    // scope mode_calcul
    scope(i2s_buf0,voices[0].frequency,14,true,true,0,0,0);     // scope mode_data

    // après démo mute voices[0] sinus
    setVoiceAmplCoders(&voices[0],WSIN,cga,0);

    gpio_irq_set = false;            // wait for the touch button to start
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


void ledblinkn(uint8_t n){
    if(
        (led==0 && (millisCounter-ledBlinker)>(durOffOn[led]-durOffOn[led+1]-(n-1)*(durOffOn[led+2]+durOffOn[led+3]))) 
        || 
        (led!=0 && (millisCounter-ledBlinker)>(durOffOn[led]))
    )
    {

/*// test pour claude
static uint32_t last_c2=0xFFFFFFFF;
static uint32_t last_c3=0xFFFFFFFF;
uint32_t c2=dma_hw->ch[2].ctrl_trig;
uint32_t c3=dma_hw->ch[3].ctrl_trig;
if(c2!=last_c2 || c3!=last_c3){
    printf("DMA2/3: c2=%08x c3=%08x\n",c2,c3);
    last_c2=c2;
    last_c3=c3;
}*/

        if(n>MAXBLK){n=MAXBLK;}
        ledBlinker=millisCounter;
        if(led<((2*n)-1)){led++;}
        else led=0;
        gpio_put(LED,led&0x01);
    }
}
