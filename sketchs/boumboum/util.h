#ifndef _UTIL_H_
#define _UTIL_H_ 

#include "hardware/pio.h"

void setup();
void print_diag();
void print_diag(char c);
void print_diag(char c,uint32_t gdis);

void pio_full_reset(PIO pio);

void dumpStr(int32_t* str,uint32_t nb);
void dumpStr(char* data,uint32_t len);
void dumpfield(char* fd,uint8_t ll);
void show_cnt(uint32_t cnt,uint16_t x,uint16_t y);
void show_cnt(uint32_t cnt,uint16_t x,uint16_t y,uint8_t mult);

char getCh();
uint8_t getNumCh(char min,char max);

int convIntToString(char* str,int32_t num,uint8_t len);
int convIntToString(char* str,int32_t num);
int convNumToString(char* str,float num);
uint8_t conv_atob(const char* ascii,uint16_t* bin,uint8_t len);
uint8_t conv_atobl(const char* ascii,uint32_t* bin,uint8_t len);
void conv_atoh(char* ascii,uint8_t* h);
void conv_htoa(char* ascii,uint8_t* h);
void conv_htoa(char* ascii,uint8_t* h,uint8_t len);
uint32_t convStrToHex(char* str,uint8_t len);
float convStrToNum(char* str,int* sizeRead);
int32_t convStrToInt(char* str,int* sizeRead);

void global_dma_irq_handler();

void adsr(int32_t* ccb,int32_t ccb0);
void autoMixer(int32_t* ccb,int32_t ccb0);

void quick_delay(uint32_t us);
void delay_ms(uint32_t ms);
void delayBlk(uint8_t sec);
void ledblinkn(uint8_t n);

void print_memory_report(void);
void system_error(const char* s);
uint32_t signal_overflow(const char* s,uint16_t id,int32_t val,uint32_t max);


#endif  //_UTIL_H_