#ifndef _MENUS_H_
#define _MENUS_H_

void inputsInit();
void menus_init();
uint8_t coders_for_wavesAmpl(uint8_t currVoice);
uint8_t coders_for_freq(uint8_t currVoice);
uint8_t coders_for_genAmpl(uint8_t currVoice);
uint8_t coders_for_lfos_freq();
uint8_t coders_for_mapping();
uint8_t coders_for_menu();


#endif // _MENUS_H_