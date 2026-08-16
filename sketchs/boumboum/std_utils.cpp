#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <malloc.h>

#include "hardware/pio.h"
#include "pico/stdlib.h"
#include "std_utils.h"
#include "const.h"

extern volatile uint32_t millisCounter;

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
