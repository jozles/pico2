#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/timer.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "bb_i2s.h"
#include "const.h"
#include "util.h"
#include "coder.h"
#include "frequences.h"
#include "test.h"
#include "leds.h"
#include "st7789.h"

// boumboum

static int st_dma_channel;
static int ws_dma_channel;

extern struct Voice voices[];

volatile uint8_t what=0;

#define W_TEST 1    // test simple ; sinus continu depuis buffer rempli une fois (1/32)
#define W_SINUS 2   // sinus continu calculé à la volée

extern volatile uint32_t millisCounter;

volatile uint32_t durOffOn[]={LEDOFFDUR,LEDONDUR};
volatile bool led=false;
volatile uint32_t ledBlinker=0;

static repeating_timer millisTimer;

float amplIncr[MAX_16B_LINEAR_VALUE];


// ******** coders functions handling ********

#ifdef MUXED_CODERS
int8_t ccbChanged(int32_t* ccb,int32_t ccb0){
    for(uint8_t i=0;i<CODER_NB;i++){if(ccb[i]!=ccb0[i]){return i;}}
    return -1;
}

void adsr(int32_t* ccb,int32_t ccb0){}

// 16 bits values with constant sum
void autoMixer(int32_t* ccb,uint32_t ccb0){
    int8_t c=ccbChanged(int32_t* ccb,uint32_t ccb0);
    if(c<0){return;}                                    // no change
    int32_t v=ccb[i]-ccb0[i];
    if(v>0){
        for(uint8_t k=0;k<CODER_NB;k++){
            if(ccb[k]>(MAX_16B_LINEAR_VALUE-1-v)){ccb[c]=ccb0[c];return;}      // no change : value in excess
        }
    }
    else {
        for(uint8_t k=0;k<CODER_NB;k++){
            if(ccb[k]<=1+v){ccb[c]=ccb0[c];return;}               // no change : value in excess
        }
    }
    for(uint8_t i=0;i<CODER_NB;i++){
        if(i==c){ccb[i]=ccb0[i]+v*(CODER_NB-1);}
        else ccb[i]+=v;
    }
    // ccb 16bits linear values to be changed to exponential (ie v*2^n)
}
#endif// MUXED_CODERS

// ******** global setup ********


bool millisTimerHandler(repeating_timer *t){
    millisCounter++;
    coderTimerHandler();
//    if(millisCounter%1000==0){
//        tft_draw_int_12x12_dma_mult(165,12,0xffff,0x0000,1,millisCounter/1000);}
//        tft_draw_int_12x12_dma_mult(180,12,0xffff,0x0000,1,dma_tfr_count);}
    return true;
}

#ifdef GLOBAL_DMA_IRQ_HANDLER

void global_dma_irq_handler(){
    
    uint32_t global_dma_irq_status = dma_hw->intr;

    if((global_dma_irq_status & (1u << ws_dma_channel))!=0){
        ws_dma_irq_handler();
    } 
    if((global_dma_irq_status & (1u << st_dma_channel))!=0){
        st_dma_irq_handler();
    } 
}

void init_global_dma_irq(){
    irq_set_exclusive_handler(DMA_IRQ_1, global_dma_irq_handler);
    irq_set_enabled(DMA_IRQ_1, true);
}
#endif  // GLOBAL_DMA_IRQ_HANDLER

void setup(){

    gpio_init(LED);gpio_set_dir(LED,GPIO_OUT); gpio_put(LED,LOW);

    gpio_init(TEST_PIN);gpio_set_dir(TEST_PIN,GPIO_OUT); gpio_put(TEST_PIN,LOW);

    gpio_init(PIN_DCDC_PSM_CTRL);gpio_set_dir(PIN_DCDC_PSM_CTRL, GPIO_OUT);
    gpio_put(PIN_DCDC_PSM_CTRL, 1); // PWM mode for less Audio noise
    
    #ifndef MUXED_CODER
    coderInit(CODER_GPIO_CLOCK,CODER_GPIO_DATA,CODER_GPIO_SW,CODER_GPIO_VCC,CODER_TIMER_POOLING_INTERVAL_MS,CODER_STROBE_NUMBER);
    #endif  // MUXED_CODER
    #ifdef MUXED_CODER
    coderInit(CODER_GPIO_CLOCK,CODER_GPIO_DATA,CODER_GPIO_SW,CODER_GPIO_VCC,CODER_PIO_SEL0,CODER_SEL_NB,CODER_NB,CODER_TIMER_POOLING_INTERVAL_MS,CODER_STROBE_NUMBER);
    #endif // MUXED_CODER
    
    // irq timer
    add_repeating_timer_ms(1, millisTimerHandler, NULL, &millisTimer);

    fillBasicWaveForms();
    freq_start();

    uint8_t channel=0;
    voiceInit(440,&voices[channel]);

    what=W_SINUS;

    bb_i2s_start();

    ws_dma_channel=ledsWs2812Setup(ws2812_pio,WS2812_LED_PIN);
    if(ws_dma_channel<0){LEDBLINK_ERROR_DMA}

    st_dma_channel=st7789_setup(ST7789_SPI_SPEED);
    if(st_dma_channel<0){LEDBLINK_ERROR_DMA}
 
    #ifdef GLOBAL_DMA_IRQ_HANDLER
    init_global_dma_irq();
    #endif

    tft_fill(0x000000);
    uint8_t m=3;
    tft_draw_text_12x12_dma_mult((TFT_W-(6*10*m))/2,(TFT_H-m*10)/2, "ST7789", 0xFFFF, 0x0000,m);
    uint8_t ls=16;
    char s[ls];memset(s,0x00,ls);
    convIntToString(s,TFT_W);s[3]='x';convIntToString(s+4,TFT_H);
    tft_draw_text_12x12_dma_mult((TFT_W-(7*10))/2,TFT_H/2+14,s, 0xFFFF, 0x0000,1);
    sleep_ms(5000);
    tft_fill(0x000000);

    printf("end setup \n",st_dma_channel,get_st_dma_done());
    print_diag();

}

// -----------------------------
// ******** i2s feeding ********
// -----------------------------

    // exclusively called by void i2s_callback_func() in bb_i2s.cpp 
    // and dependancies _ see bb_i2s.cpp
void next_sound_feeding(int32_t* next_sound,uint32_t next_sound_size){

        if(next_sound_size!=SAMPLES_PER_BUFFER){
            printf("next_sound_size:%d != SAMPLES_PER_BUFFER:%d\n",next_sound_size,SAMPLES_PER_BUFFER);
            LEDBLINK_ERROR;
            return;
        }

    switch (what){
        case W_TEST:    
        test_next_sound_feeding(next_sound,next_sound_size);
            break;

        case W_SINUS:
        fillVoiceBuffer(next_sound,&voices[0]);
            break;
        
        default:
            break;
    }
}


// ******** diags/debug ********

void pd0(){
    printf("st_dma_chan=%d, st_dma_done=%d\n",st_dma_channel,get_st_dma_done());
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

    //printf("Dump PIO0:\n");
    //for (int i = 0; i < 32; i++) {
    //    printf("instr[%02d] = 0x%04x\n", i, pio0->instr_mem[i]);
    //}

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

  t=convIntToString(str,(int)num);

  num=num-(int)num;
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

void dumpstr0(char* data,uint8_t len,bool cr)
{
    char a[]={0x00,0x00,0x00};
    uint8_t c;
    printf("   %x   ",(long)data);
    for(int k=0;k<len;k++){conv_htoa(a,(uint8_t*)&data[k]);printf(" %c",a);}
    printf("    ");
    for(int k=0;k<len;k++){
            c=data[k];
            if(c<32 || c>127){c='.';}
            printf("%c",(char)c);
    }
    if(cr){printf("\n");}
}

void dumpstr(char* data,uint16_t len,bool cr)
{
    while(len>=16){len-=16;dumpstr0(data,16,len>0);data+=16;}
    if(len!=0){dumpstr0(data,len,false);}
    if(cr){printf("\n");}
}

void dumpstr(char* data,uint16_t len)
{
  return dumpstr(data,len,true);
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