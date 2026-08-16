#ifndef _STD_UTILS_H_
#define _STD_UTILS_H_

void print_memory_report(void);

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

#endif // _STD_UTILS_H_