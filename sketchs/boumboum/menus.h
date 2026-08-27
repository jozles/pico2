#ifndef _MENUS_H_
#define _MENUS_H_

void inputsInit();
void menus_init();
uint8_t coders_for_wavesAmpl(uint8_t currVoice);
uint8_t coders_for_freq(uint8_t currVoice);
uint8_t coders_for_genAmpl(uint8_t currVoice);
uint8_t coders_for_lfos_freq();
uint8_t coders_for_mapping();
uint8_t coders_for_menu(const char* title,const char* menu,uint8_t linesNb,uint8_t line_len,uint8_t type,volatile int16_t* cTC, volatile bool* cTS, volatile bool *ccTB,uint16_t *maxi,uint16_t** var,uint8_t varNb,uint8_t coderNb,uint8_t line0);

#ifdef __cplusplus
extern "C" {
#endif

extern const char menu0_names[][MENU_NAME_LEN];

#ifdef __cplusplus
}
#endif
#endif // _MENUS_H_