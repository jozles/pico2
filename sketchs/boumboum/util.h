#ifndef _UTIL_H_
#define _UTIL_H_ 

#include "hardware/pio.h"

void setup();
void testSetup();
void print_diag();
void print_diag(char c);
void print_diag(char c,uint32_t gdis);

void pio_full_reset(PIO pio);

void global_dma_irq_handler();
void pwm_timer_1khz_enable(bool start_stop);

void adsr(int32_t* ccb,int32_t ccb0);
void autoMixer(int32_t* ccb,int32_t ccb0);

void quick_delay(uint32_t us);
void delay_ms(uint32_t ms);
void delayBlk(uint8_t sec);
void ledblinkn(uint8_t n);

//void print_memory_report(void);
void system_error(const char* s);
void system_error(const char* s,int32_t v);
uint32_t signal_overflow(const char* s,uint16_t id,int32_t val,int32_t min,int32_t max);

void blank(void* s,uint32_t len);

void init_out_anal();
void out_anal(uint32_t val_u32);

#ifdef __cplusplus
extern "C" {
#endif
// assembleur 290uS pour 240*240*2 bytes ; la zone effacée doit etre alignée 32 bytes!!!
// alignement 16 bytes : 336uS
// memset 1.28mS ;
void __not_in_flash_func(blank_)(void *var, uint32_t len,uint8_t c);   
#ifdef __cplusplus
}
#endif

#endif  //_UTIL_H_