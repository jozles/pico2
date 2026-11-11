#ifndef _CONST_H_
#define _CONST_H_

#include <stdint.h>
#include "pico/stdlib.h"
#include "util.h"

#include "rc_33tables.h"

#define VERSION "v1.4d"

#define PI 3.14159265359

#define BB_TEST_MODE

#define TST_PIN 0

#define GP_ANAL_PIN 6   // gpio analog output

#define BUT_VCC_PIN 28
#define BUTTON_PIN  27
#define BUTTON_CODE -100

/* slices */

#define PWM_IRQ_SLICE 0

/* spin_locks */

#define DMA_LOCK 5
#define WS_LOCK  3
#define CTL_INPUTS_LOCK 7

/* coders */

/*  ******************** Coders: hardware and configuration ********************

    Reading
      With CPLD defined, the coders are read through the CPLD (ATF1508AS). At each poll, the Pico
      pulses CLR (the CPLD counter goes to position 0), then for each position it reads the result
      lines and pulses CLK to move to the next position. Lines read at each position: CK (OCK),
      DT (ODT), SW (OSW) and the touch button of that position (OTB).
      Without CPLD, the coders are read through 4051 multiplexers (former boards).
      Polling: every CODER_TIMER_POLLING_INTERVAL_MS (1 ms). A scan of the 18 positions took 11 us
      before the overlapped reading of coder.cpp (to be measured again).

    Physical configuration: one table per board, coders_cpld.def or coders_4051.def
      For each position: logical numbers of the coder and of the touch button, type of coder,
      level given by a pressed switch, direction. Coder types and switch wirings can be mixed.

    Coder types
      - Blue coders, bare, 20 detents: one full cycle of CK and DT per detent, rest at 11: CT_FULL.
      - Green coders, bare, 30 detents, and green coders on black modules (Keyes): half a cycle
        per detent, rest alternately at 11 and 00: CT_HALF.
      Each detent gives +1 or -1 on the value of the coder.
      Decoding (coder.cpp): from the state of CK and DT. A detent is made of 4 changes of state
      (CT_FULL) or 2 (CT_HALF); no debouncing is needed, since a contact bounce (up to 100 us)
      gives a change then its reverse. A state skipped between two polls is taken in the direction
      of the last move. Limits simulated at 1 ms polling, with 100 us bounces: about 280 detents/s
      for the blue coders (fastest gesture measured: 233 detents/s), about 450 detents/s for the
      green ones (symmetric model, to be checked by measurement).
      Direction: CK falls first when turning clockwise, and the value goes up.

    Switches
      codersSw (set by coderSetup) holds ON_SWITCH when the switch is pressed and !ON_SWITCH when it
      is released, whatever its wiring: the sw column of the table gives the level of a pressed switch.

    Wiring
      The CPLD has no internal pull-up or pull-down resistors: every coder line needs one (10 kohm).
      All pull-ups go to 3.3 V (VCCIO of the CPLD), never to 5 V.
      - Printed board: the same wiring for every bare coder (no modules): all the contacts to ground,
        CK, DT and SW pulled up to 3.3 V: SW_LOW for every coder.
      - Proto board, bare coders: common pin (middle of the 3-pin side) to ground, CK and DT pulled
        up to 3.3 V; switch between SW and 3.3 V, SW pulled down to ground: SW_HIGH.
      - Modules: they carry the pull-ups of CK and DT; their "+" pin goes to 3.3 V. Their switch goes
        to ground, so SW needs a pull-up to 3.3 V (often missing on the module): SW_LOW.
      - Unused coder and touch button inputs of the CPLD: to ground, without any resistor.

    *****************************************************************************/

#define CPLD                               // coders read through the CPLD (comment out for the 4051 multiplexers)

// columns of coders_cpld.def and coders_4051.def
#define CT_NONE                 0          // type: no coder at this position
#define CT_HALF                 2          // type: half a cycle per detent (green coders); value = changes of state per detent
#define CT_FULL                 4          // type: one full cycle per detent (blue coders); value = changes of state per detent
#define SW_LOW                  0          // a pressed switch gives a low level
#define SW_HIGH                 1          // a pressed switch gives a high level
#define DIR_NORM                0          // CK falls first clockwise
#define DIR_REV                 1          // CK and DT swapped

#define CODER_IS_REST(st,q)     (((st)&((q)-1))==0)    // rest positions: 11 (q = 4), 11 and 00 (q = 2)
#define CODER_DIR_SKIP          2          // coderDir: full-cycle coder, one state skipped from rest, direction not known yet
#define CODER_DIR_HOLD_MS       5          // direction of the last move kept this long (used when a state is skipped)

#define CODER_TIMER_POLLING_INTERVAL_MS 1  // timer polling interval in milliseconds
#define CODER_STROBE_NUMBER     3          // unused since the decoding by state (still passed to coderInit)
#define CODER_SW_STROBE_MS      50         // minimum delay between two valid switch changes (debouncing)
#define ON_SWITCH               1          // value of codersSw for a pressed switch, whatever its wiring

// unused if CPLD

#define CODER_GPIO_VCC         22
#define CODER_PIO_SEL0          2       // CODER_SEL_NB consecutive pins
#define CODER_SEL_NB            4       // selection bits

#ifndef CPLD

#define MUXED_TOUCH_PIN        26

#define CODER_GPIO_CLOCK       10          
#define CODER_GPIO_DATA        11          
#define CODER_GPIO_SW          12

#define CODER_NB                8
#define CODER_BANK_NB           6

#endif  // !CPLD

#ifdef CPLD

#define CODER_GPIO_CLOCK       12       // CPLD 81       
#define CODER_GPIO_DATA        11       // 83
#define CODER_GPIO_SW          10       // 85

#define CODER_NB               18
#define CODER_CLR_CNT_PIN       8       // 89
#define CODER_CLK_CNT_PIN       9       // 87

#define MUXED_TOUCH_PIN         7       // 93

#endif  // CPLD

// *** coders actions ***

#define CODER_FREQUENCY         0       // voice frequency coder in all functions 

#define LOW 0
#define OFF 0
#define HIGH 1
#define ON 1

/* Menus */

#define MENU_NAME_LEN 12
typedef enum {
#define Z(name,text) name,    
#include "menu.def"
        MENU0_NB
#undef Z
}  Menu;

#define MAX_STEP_FRA 65536  // décimales ratio sampleFreq/currFreq/adsr

#define MAX_16B_LINEAR_VALUE 32     // 0 to 31 => 0,1,1.414,2,2.828,4,5.656,8,11.312,16,22.624 ... 8192,11583,16384,23167,32767
#define MIN_16B_LINEAR_VALUE 0

#define WAVE_LEVEL_STEPS 64                         // number of steps of the wave amplitude coders (0 = off)


/* Voices */

#define MAX_VOICES 8 
#define VCES_MAX_FREQ_CODERS 4498 //4399
#define VCES_MIN_FREQ_CODERS 25
#define VCES_MAX_AMPL_CODERS MAX_16B_LINEAR_VALUE
#define GAIN_REDUC 8                    // gain reduction shift in fillVoices

/* lfos */

#define MAX_LFO 8
#define LFOS_SAMPLE_RATE 40
#define LFOS_MAX_FREQ_CODERS VCES_MAX_FREQ_CODERS //3000
#define LFOS_MIN_FREQ_CODERS 420        // 30sec
#define OSC_SCOPE_BUFFER_LEN 256        // SAMPLE_BUFFER_SIZE         // OSC_SCOPE
#define VOICE_FREQ_DIVIDER  1024        // pour usage conjoint de calcFreq()

/* Adsrl */

#define MAX_ADSR 8
#define ADSR_SAMPLE_RATE 500
#define DUR_ECH_NB 128                  // puissance de 2 !!! définit le pas dans les tables d'échantillons 
#define ADSR_MAX_TIME_CODERS (DUR_ECH_NB-1)       // valeur maxi saisie par les coders      
#define ADSR_MAX_LEVEL_CODERS (MAX_16B_LINEAR_VALUE-1)
#define ADSR_SCOPE_BUFFER_LEN TFT_W

/* touch buttons */

#define MAX_TBUT CODER_NB       // use coder poling

/* lf mixers */

#define MAX_LFM 4
//#define MAX_LFM_INPUTS 3        // 0: gen ; 1,2 individual
#define LFM_SCOPE_BUFFER_LEN TFT_W
#define LFM_SAMPLE_RATE 25      // mS

/* led */

#define LEDBLINK  if((millisCounter-ledBlinker)>durOffOn[led]){ledBlinker=millisCounter;led=!led;gpio_put(LED,led);}
#define LEDBLINK_ERROR      durOffOn[0]=2*durOffOn[1];LEDBLINK
#define LEDBLINK_ERROR_DMA  durOffOn[0]=2*durOffOn[1];while(1){gpio_put(LED,led);sleep_ms(durOffOn[led]);gpio_put(LED,!led);sleep_ms(durOffOn[!led]);}
#define LEDONDUR 60
#define LEDOFFDUR 1000

#define LED 25              // pico2 built_int led
#define LED_PIN LED

/* dma */

#define GLOBAL_DMA_IRQ_HANDLER

/* Ws2812 */

#define PICO_WS2812_PIO 1
#define ws2812_pio __CONCAT(pio, PICO_WS2812_PIO)
#define WS2812_LED_PIN 1    // up to 4 wS2812 led chains on pico2 with the same pio 
                            // each one using a different state machine (return from ledsWs2812Setup())
                            // voir commentaire dans main
#define WS2812_DREQ_PIO __CONCAT(DREQ_PIO,PICO_WS2812_PIO)
#define WS2812_DREQ_PIO_TX0 __CONCAT(WS2812_DREQ_PIO,_TX0)

/* I2S */

#define PICO_I2S_PIO 0
#define _i2s_pio __CONCAT(pio, PICO_I2S_PIO)   
#define PICO_AUDIO_I2S_DATA_PIN 13                      // 2 consecutive gpios
#define SAMPLE_RATE 44100

/* frequencies/voices */

#define SAMPLE_F SAMPLE_RATE                            // fréquence d'échantillonnage audio   
#define SAMPLE_PER ((float)1/SAMPLE_F)

#define SAMPLES_PER_BUFFER 512                          // nombre d'échantillons (L+R) par buffer (1024 trop lent) 
static_assert((SAMPLES_PER_BUFFER % 4) == 0, "the unrolled index loop writes 4 samples per pass"); 

#define RC_TABLES_LEN RC_N_SAMPLES                      // ***** POWER OF 2 *****  nombre d'échantillons dans les 1/2 tables d'ondes
#define RC_TABLES_NB RC_N_TABLES                        // nombre de tables RC (MAXCODER_RC possible values)
#define CODER_CR_OFFSET RC_TABLES_NB
#define MAXCODER_RC ((RC_TABLES_NB-1)*2)                // DOIT ETRE PAIR (-31 0 +31 : 63 values 0-62 ) le nombre total de tables doit être impair pour le mirroring
#define MINCODER_RC 0

#define BASIC_WAVE_TABLE_LEN (RC_TABLES_LEN*2)          // ***** POWER OF 2 *****  nombre d'échantillons dans les tables d'ondes
#define BASIC_WAVE_TABLE_POW 11                         // ***** POWER OF 2 *****  nombre d'échantillons dans les tables d'ondes
static_assert((1u << BASIC_WAVE_TABLE_POW) == BASIC_WAVE_TABLE_LEN, "BASIC_WAVE_TABLE_POW and BASIC_WAVE_TABLE_LEN are inconsistent");

/* ECHO */

#define MAX_ECHO 1
#define ECHO_BUF_LEN 22050
#define ECHO_STEPS (SAMPLES_PER_BUFFER/2)     // echo steps per audio buffer (22050 Hz)

// the echo runs at 22050 Hz: one step = one pair of samples of the audio buffer
#define ECHO_STEPS_SHIFT  8                                     // log2(ECHO_STEPS)
static_assert((1 << ECHO_STEPS_SHIFT) == ECHO_STEPS, "ECHO_STEPS_SHIFT must be log2(ECHO_STEPS)");
static_assert(((ECHO_BUF_LEN * 2) % 4) == 0, "the DMA clears bufEcho with 32-bit words");

#define ECHO_MIN_DELAY    1438                                  // 65 ms at 22050 Hz: delay of coder step 0 (shorter delays saturate)
#define ECHO_RANGE_DB     30.0f 
#define ECHO_FB_STEP1_DB  30.0f                                 // loss per repetition at feedback step 1 (the top step loses what ECHO_FB_MAX_Q15 gives)
#define ECHO_DUR_SHIFT    8                                     // input shift for the 128 steps of the duration (whole range with a full-scale input)
#define ECHO_INJ_STEPS    64                                    // steps of the feedback coder
#define ECHO_INJ_SHIFT    9                                     // input shift for the 64 steps of the feedback
#define ECHO_FB_MAX_Q15   29491                                 // highest feedback: 0.9 (Q15); 1.0 or more would grow until saturation
#define ECHO_FADE_SHIFT   10                                    // crossfade between the old and the new delay: 1024 steps = 46 ms
#define ECHO_FADE_LEN     (1 << ECHO_FADE_SHIFT)
#define ECHO_SILENCE      4                                     // about -72 dB below a full-scale voice (16384 in bufEcho)

/* REVERB */

#define MAX_REV 1
#define REV_BUF_LEN 5418 // 22050 Hz, int32: the 10 lines of the reverb ;

#define REVB_FDN          8                                     // coupled delay lines of the network
#define REVB_ALLPASS      2                                     // input allpass filters in series
#define REVB_LINES        (REVB_FDN + REVB_ALLPASS)
#define REVB_MAX_LINE     887                                   // longest line, in steps
#define REVB_DIFF_Q15     19661                                 // gain of the input allpass filters: 0.6 (Q15)
#define REVB_RT60_MIN     0.3f                                  // decay time (-60 dB) of duration step 0, in s
#define REVB_RT60_MAX     5.0f                                  // decay time of the top duration step, in s
#define REVB_DUR_SHIFT    8                                     // input shift for the 128 steps of the duration
#define REVB_SHI_STEPS    64                                    // steps of the brightness coder
#define REVB_SHI_SHIFT    9                                     // input shift for the 64 steps of the brightness
#define REVB_DAMP_MAX_Q15 22938                                 // damping at brightness 0: 0.7 (Q15)
#define REVB_IN_SHIFT     8                                     // bus to line input: a full-scale voice gives 2^22, about 30 dB of headroom left in the int32 lines
#define REVB_OUT_RSHIFT   9                                     // output gain: one unit = 6 dB ; a loud voice at a long decay can reach full scale: lower the return.
#define REVB_SILENCE      1024                                  // output below this (about -72 dB below a full-scale voice): the reverb stops

/* tests: a defined TEST_xxx runs its test at start-up (testSetup, tests.cpp) */
//#define TEST_REVERB                                             // reverb test: test_reverb()
//#define REVB_TEST_LOG_LEN 700                                   // reverb test: buffers logged (700 x 11.6 ms = 8.1 s)
#define OLD_TEST
//#define TEST_PING
//#define TEST_A

/* inputs/outputs */

#define MAX_OUTPUT_OBJ          (MAX_VOICES+MAX_LFO+MAX_ADSR+MAX_TBUT+MAX_LFM)
#define MAX_OUTPUTS_PER_OBJ     8
#define MAX_OUTPUTS             (MAX_OUTPUT_OBJ*MAX_OUTPUTS_PER_OBJ)
#define MAX_OUTPUTS_REAL        (TOTAL_VOICES_OUTPUTS_NB+TOTAL_LFO_OUTPUTS_NB+TOTAL_ADSR_OUTPUTS_NB+TOTAL_TBUT_OUTPUTS_NB+TOTAL_LFM_OUTPUTS_NB)
#define MAX_INPUT_OBJ           (MAX_VOICES+MAX_LFO+MAX_ADSR+MAX_TBUT+MAX_LFM+MAX_REV+MAX_ECHO)

#define TOTAL_INPUTS_REAL       (TOTAL_VOICES_INPUTS_NB+TOTAL_LFO_INPUTS_NB+TOTAL_ADSR_INPUTS_NB+TOTAL_LFM_INPUTS_NB+TOTAL_REV_INPUTS_NB+TOTAL_ECHO_INPUTS_NB)
#define TOTAL_INPUTS_ID_REAL    (TOTAL_INPUTS_REAL+1)
#define NO_LINK                 (-1)
#define OBJ_IO_NAME_LEN         5
#define IN_OUT_NAME_LEN         10

#define MAX_CTL_ATT             256     
#define MAX_CTL_ATT_SHIFT       __builtin_ctz(MAX_CTL_ATT)
static_assert(MAX_CTL_ATT == (1 << MAX_CTL_ATT_SHIFT), "MAX_CTL_ATT and MAX_CTL_ATT_SHIFT are inconsistent");
#define CODER_BASE_SHIFT (15 - MAX_CTL_ATT_SHIFT)   // maps an N-bit base coder onto the same signed 16-bit scale as the attenuated term
static_assert(CODER_BASE_SHIFT > 0 && CODER_BASE_SHIFT < 15, "MAX_CTL_ATT_SHIFT out of the range CODER_BASE_SHIFT can handle");

#define NO_ATTENUATION_SHIFT    15
//#define NO_ATTENUATION_VALUE    0x7fff
#define FULL_ATTENUATION_VALUE  0x0000

#define BASIC_WAVES_NB 6

/* Objects */

typedef enum {
#define Z(name,text) name,    
#include "objects.def"
        OBJECT_TYPES_NB
#undef Z
}  objects;

#define OBJECTS_NAME_LEN 8

// ****** inputs ******

typedef enum {
#define X(name,text) name,    
#include "vces_inputs_names.def"
        VOICES_INPUTS_NB
#define TOTAL_VOICES_INPUTS_NB (VOICES_INPUTS_NB*MAX_VOICES)
#undef X
}  Vces_inputs_names;

typedef enum {
#define X(name,text) name,    
#include "lfos_inputs_names.def"
        LFO_INPUTS_NB
#define TOTAL_LFO_INPUTS_NB (LFO_INPUTS_NB*MAX_LFO)          
#undef X
}  Lfo_inputs_names;

typedef enum {
#define X(name,text) name,    
#include "adsr_inputs_names.def"
        ADSR_INPUTS_NB 
#define TOTAL_ADSR_INPUTS_NB (ADSR_INPUTS_NB*MAX_ADSR)         
#undef X
}  Adsr_inputs_names;

typedef enum {
#define X(name,text) name,    
#include "norm_types.def"
        NORM_TYPES_NB          
#undef X
}  Norm_types_names;

typedef enum {
#define X(name,text) name,    
#include "lfm_inputs_names.def"
        LFM_INPUTS_NB
#define TOTAL_LFM_INPUTS_NB (LFM_INPUTS_NB*MAX_LFM)          
#undef X
}  Lfm_inputs_names;

typedef enum {
#define X(name,text) name,    
#include "rev_inputs_names.def"
        REVB_INPUTS_NB 
#define TOTAL_REV_INPUTS_NB (REVB_INPUTS_NB*MAX_REV)         
#undef X
}  Rev_inputs_names;

typedef enum {
#define X(name,text) name,    
#include "echo_inputs_names.def"
        ECHO_INPUTS_NB
#define TOTAL_ECHO_INPUTS_NB (ECHO_INPUTS_NB*MAX_ECHO)          
#undef X
}  Echo_inputs_names;

// ****** outputs ******

typedef enum {
#define X(name,text) name,    
#include "lfos_outputs_names.def"
        LFO_OUTPUTS_NB
#define TOTAL_LFO_OUTPUTS_NB (LFO_OUTPUTS_NB*MAX_LFO)          
#undef X
}  Lfo_outputs_names;

typedef enum {
#define X(name,text) name,    
#include "adsr_outputs_names.def"
        ADSR_OUTPUTS_NB
#define TOTAL_ADSR_OUTPUTS_NB (ADSR_OUTPUTS_NB*MAX_ADSR)          
#undef X
}  Adsr_outputs_names;

typedef enum {
#define X(name,text) name,    
#include "vces_outputs_names.def"
        VCES_OUTPUTS_NB 
#define TOTAL_VOICES_OUTPUTS_NB (VCES_OUTPUTS_NB*MAX_VOICES)         
#undef X
}  Vces_outputs_names;

typedef enum {
#define X(name,text) name,    
#include "tbut_outputs_names.def"
        TBUT_OUTPUTS_NB
#define TOTAL_TBUT_OUTPUTS_NB (TBUT_OUTPUTS_NB*MAX_TBUT)         
#undef X
}  Tbut_outputs_names;

typedef enum {
#define X(name,text) name,    
#include "lfm_outputs_names.def"
        LFM_OUTPUTS_NB 
#define TOTAL_LFM_OUTPUTS_NB (LFM_OUTPUTS_NB*MAX_LFM)         
#undef X
}  Lfm_outputs_names;


// ****** basics waves codes ******

#define FIRST_WAVE W_SINUS
#define LAST_WAVE W_SQUARE 

#define PIN_DCDC_PSM_CTRL 23        // to set the DCDC in PSM mode for less audio noise

/* display */

// Colours, as displayed by the screen (BGR order on both the ST7789 and the ESP32 panel:
// blue in the high bits, red in the low bits)
#define RGB565(r, g, b)     ((uint16_t)((((b) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((r) >> 3)))
#define COL_BLACK           0x0000
#define COL_WHITE           0xFFFF
#define COL_RED             0x001F
#define COL_GREEN           0x07E0
#define COL_BLUE            0xF800
#define COL_YELLOW          0x07FF
#define COL_CYAN            0xFFE0
#define COL_MAGENTA         0xF81F
#define COL_GREY            0x7BEF
#define COL_ORANGE          0x05BF

//#define ST7789

#define TFT_W 320
#define TFT_H 240

#ifdef ST7789

#define DISP_PIN_SCK   18
#define DISP_PIN_MOSI  19
#define DISP_PIN_CS    17
#define DISP_SPEED     ST7789_SPI_SPEED

#define CHAR_W  12      // ST7789 font 12x12: character cell width (pixels, size 1)
#define CHAR_H  12      // ST7789 font 12x12: character cell height

// le dma utilise l'irq1 ; l'irq 0 est utilisée par le dma de l'i2s

#define ST7789_PIN_DC    16
#define ST7789_PIN_RST   20
#define ST7789_PIN_BL    21

#endif  // ST7789

#ifndef ST7789

// Câblage : Pico GP16 vers P3 IO35, P3 IO22 vers Pico GP18, et GND vers GND.

#define DISP_UART      uart0
#define DISP_PIN_TX    16       // to the ESP32 IO35 (its UART RX)
#define DISP_PIN_CTS   18       // from the ESP32 IO22 (high: ESP32 cannot receive)
#define DISP_PIN_RX    17       // reserved: data from the ESP32 (config reads), not wired yet
#define LNK_UART_BAUD  4000000  // 2000000 if errors appear (same value on the ESP)
#define DISP_SPEED     LNK_UART_BAUD

#define CHAR_W  8       // ESP32 font: character cell width (pixels, size 1)
#define CHAR_H  16      // ESP32 font: character cell height

// ---------- link to the ESP32 display (UART, pins and speed in the DISP_ section) ----------
// packet = LNK_SYNC, length of the orders (2 bytes, low first), orders, sum of the order bytes (8 bits)
// order  = letter, n (2 bytes, low first), n data bytes
#define LNK_SYNC            0xA5
#define LNK_PKT_MAX         2048        // largest orders part of a packet (same value on the ESP)
#define LNK_PKT_HDR         3           // LNK_SYNC + 2-byte length
#define LNK_HDR_SIZE        3           // order: letter + 2-byte length

// Command letters
#define LNK_CMD_TEXT        'T'   // UTF-8 text at the current position
#define LNK_CMD_FONT        'P'   // size multiplier (1 byte)
#define LNK_CMD_POS         'A'   // x, y
#define LNK_CMD_CLEAR       'C'   // x, y, w, h filled with bg
#define LNK_CMD_COLOR       'K'   // fg, bg (RGB565)
#define LNK_CMD_GRAPH       'G'   // pixels: x, y, colour
#define LNK_CMD_ZONE        'Z'   // scrolling zone: y (2 bytes), number of lines (1), pitch in pixels (1)
#define LNK_CMD_UP          'U'   // scroll up, new line (UTF-8 text) written at the bottom
#define LNK_CMD_DOWN        'D'   // scroll down, new line written at the top

#define LNK_FONT_MAX        4     // size multiplier 1..4
#define LNK_TEXT_MAX        160   // bytes of UTF-8 text per order
#define LNK_GRAPH_MAX_PIX   320
#define LNK_GRAPH_PIX_SIZE  6     // x, y, colour: 2 bytes each
#define LNK_SCROLL_MAX_LINES 15

// Buffer sizes (whole packets: header + orders + sum)
#define LNK_SMALL_BUF_SIZE  (LNK_PKT_HDR + 4 * LNK_HDR_SIZE + 4 + 1 + 4 + LNK_TEXT_MAX + 1)                // K+P+A+T: 185
#define LNK_GRAPH_BUF_SIZE  (LNK_PKT_HDR + LNK_HDR_SIZE + LNK_GRAPH_MAX_PIX * LNK_GRAPH_PIX_SIZE + 1)      // 1927
#define LNK_NB_SMALL_BUF    3     // one being sent, one waiting, one being filled

// Timing
#define LNK_WAIT_TIMEOUT_US 100000    // ESP silent this long: packets dropped until it is ready again
#define LNK_POLL_US         100       // polling step while waiting (sound kept running)

#endif  //  ndef ST7789

#define LINE_SIZE           (TFT_W/CHAR_W)      // characters per line at size 1

#endif  // _CONST_H_

