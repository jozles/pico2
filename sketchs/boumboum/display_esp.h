#ifndef _DISPLAY_ESP_H_
#define _DISPLAY_ESP_H_

#include <stdint.h>
#include <stdbool.h>

int      display_esp_setup(uint32_t baud);
void     esp_dma_end();                 // called by disp_dma_irq_handler (screen.cpp)

bool     send_string(const uint8_t* s, uint16_t len);
bool     lnk_buffer_free(const uint8_t* buf);
uint32_t lnk_dropped();                 // packets dropped because the ESP did not answer

void     lnk_display(uint16_t x, uint16_t y, const char* s, uint16_t fg, uint16_t bg, int8_t mult);
void     lnk_text(const char* s);
void     lnk_font(uint8_t mult);
void     lnk_pos(uint16_t x, uint16_t y);
void     lnk_color(uint16_t fg, uint16_t bg);
void     lnk_clear(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t bg);
void     lnk_graph_set(uint8_t* buf, uint16_t i, uint16_t x, uint16_t y, uint16_t colour);
void     lnk_graph_send(uint8_t* buf, uint16_t nb_pix);
void     lnk_scroll_zone(uint16_t y, uint8_t nb_lines, uint8_t pitch, uint16_t bg);
void     lnk_scroll_up(const char* s, uint16_t fg, uint16_t bg, int8_t mult);
void     lnk_scroll_down(const char* s, uint16_t fg, uint16_t bg, int8_t mult);

#endif // _DISPLAY_ESP_H_
