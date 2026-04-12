#ifndef _MENUS_H_
#define _MENUS_H_

void inputsInit();
void menus_init();
uint8_t coders_for_wavesAmpl(uint8_t currVoice);
uint8_t coders_for_freq(uint8_t currVoice);
uint8_t coders_for_genAmpl(uint8_t currVoice);
uint8_t coders_for_lfos_freq();
uint8_t coders_for_mapping();
uint8_t coders_for_menu(const char* menu,uint8_t linesNb,uint8_t line_len);

#ifdef __cplusplus
extern "C" {
#endif

extern const char menu0_names[][MENU_NAME_LEN];

#ifdef __cplusplus
}
#endif
#endif // _MENUS_H_