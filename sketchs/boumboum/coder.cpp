#include <stdio.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "hardware/timer.h"
#include "hardware/pio.h"
#include "const.h"
#include "coder.h"
#include "util.h"
#include "st7789.h"
#include "miscControls.h"

#include "hardware/sync.h"

uint16_t coderTimerPollingInterval=1;       // delay between interrupts (ms), set by coderInit
uint8_t coderStrobeNumber=3;                // unused since the switch to state decoding
volatile int16_t* coderTimerCount=nullptr;  // pointer to the values to increment or decrement
volatile bool* coderTimerSwitch=nullptr;    // switch states: ON_SWITCH when pressed, whatever the wiring
volatile bool* coderTouchB=nullptr;         // current touch button states (on/off)
volatile uint16_t* coderCountMaxi=nullptr;  // maximum value for coderTimerCount

// physical configuration: one line per position of the scan (in RAM: read by the interrupt handler)
CoderPhys coderPhys[]={
#define CODER(co,tb,ty,sw,di) {co,tb,ty,sw,di},
#ifdef CPLD
#include "coders_cpld.def"
#else
#include "coders_4051.def"
#endif
#undef CODER
};
static_assert(sizeof(coderPhys)/sizeof(coderPhys[0])==CODER_NB,"the coder table must have CODER_NB lines");

#ifdef TEST_CODERS
// coder test (tests.cpp): log of every change of CK, DT or SW read by the handler
static_assert((CODER_LOG_LEN & (CODER_LOG_LEN-1))==0,"CODER_LOG_LEN must be a power of 2");
CoderLogEntry coderLog[CODER_LOG_LEN];
volatile uint16_t coderLogHead=0;           // next entry to write (handler)
volatile uint16_t coderLogTail=0;           // next entry to print (tests.cpp)
volatile uint32_t coderLogLost=0;           // entries lost because the log was full
uint8_t coderLogLast[CODER_NB];             // last CK DT SW of each position (0xFF: none yet)
#endif

// pico2 coders 

uint8_t gpio_clock_pin;
uint8_t gpio_data_pin;
uint8_t gpio_switch_pin;
#ifndef CPLD
uint8_t gpio_vcc_pin;
uint8_t gpio_sel0_pin;
uint8_t coder_sel_nb;
#endif
uint8_t coder_nb;
uint32_t sel_gpio_mask=0;

Coders c[CODER_NB];

extern volatile uint32_t millisCounter;

extern PIO pio;

volatile uint32_t int_counter=0;
volatile bool one_time;


// state of CK and DT in clockwise order (CK falls first clockwise): 11 = 0, 01 = 1, 00 = 2, 10 = 3
static inline __attribute__((always_inline)) uint8_t coderStateOf(bool ck,bool dt){
    return ck ? (dt ? 0 : 3) : (dt ? 1 : 2);
}

static_assert((CODER_ACC_MAX & (CODER_ACC_MAX-1))==0 && CODER_ACC_MAX>=16,"CODER_ACC_MAX must be a power of 2, at least 16");
static_assert(CODER_ACC_T1>CODER_ACC_T2 && CODER_ACC_T2>CODER_ACC_T3,"CODER_ACC_T1 > CODER_ACC_T2 > CODER_ACC_T3");
static_assert(CODER_COARSE_TOUCH<CODER_NB,"CODER_COARSE_TOUCH must be a touch button number (below CODER_NB)");
static_assert(VCES_MAX_FREQ_CODERS>=CODER_PITCH_MIN_MAXI && LFOS_MAX_FREQ_CODERS>=CODER_PITCH_MIN_MAXI,"the frequency coders must reach CODER_PITCH_MIN_MAXI");
static_assert(MAX_CTL_ATT<CODER_PITCH_MIN_MAXI,"an attenuator must stay below CODER_PITCH_MIN_MAXI");

// size of one detent, signed: on a frequency coder (maxi >= CODER_PITCH_MIN_MAXI) with the coarse touch button held,
// CODER_COARSE_PITCH (one semitone) ; otherwise x1, x4, x16 or CODER_ACC_MAX depending on the time since the previous detent,
// x1 after a pause or a change of direction ; the factor never exceeds maxi/64 (rounded down to a power of 2)
static inline __attribute__((always_inline)) int32_t coderStep(Coders* cp,uint8_t coder,int8_t detent,uint32_t now){
    int32_t n;
    uint32_t dt=now-cp->coderDetentTime;
    uint32_t mx=(coderCountMaxi!=nullptr) ? *(coderCountMaxi+coder) : 0;
    if(mx>=CODER_PITCH_MIN_MAXI && coderTouchB!=nullptr && *(coderTouchB+CODER_COARSE_TOUCH)) n=CODER_COARSE_PITCH;
    else if(detent!=cp->coderDetentDir || dt>CODER_ACC_T1) n=1;
    else{
        n=(dt>CODER_ACC_T2) ? 4 : ((dt>CODER_ACC_T3) ? 16 : CODER_ACC_MAX);
        uint32_t q=mx>>6;                                                           // maxi/64
        int32_t cap=(q==0) ? 1 : (int32_t)(1u<<(31-__builtin_clz(q)));             // largest power of 2 <= maxi/64
        if(n>cap) n=cap;
    }
    cp->coderDetentTime=now;
    cp->coderDetentDir=detent;
    return (detent<0) ? -n : n;
}

// one detent: step added to the value of the coder, kept between 0 and its maximum (stops at the limit)
static inline __attribute__((always_inline)) void coderCount(uint8_t coder,int32_t step){
    if(coderTimerCount==nullptr) return;
    int32_t v=(int32_t)*(coderTimerCount+coder)+step;
    if(v<0) v=0;
    int32_t mx=*(coderCountMaxi+coder);
    if(v>mx) v=mx;
    *(coderTimerCount+coder)=(int16_t)v;
}

bool __not_in_flash_func(coderTimerHandler)(){
    
    int_counter++;
    if(int_counter<coderTimerPollingInterval) return true;
    int_counter=0;
//uint32_t ints = save_and_disable_interrupts();gpio_put(TST_PIN,1);
    Coders* cp;
    Coders* ct;
    uint32_t currTime=millisCounter;

#ifdef CPLD
    gpio_put(CODER_CLK_CNT_PIN,0);
    gpio_put(CODER_CLR_CNT_PIN,1);
    busy_wait_at_least_cycles(20);                      // about 130 ns at 150 MHz
    gpio_put(CODER_CLR_CNT_PIN,0);                      // the CPLD counter is at position 0
    busy_wait_at_least_cycles(20);                      // so the result lines of position 0 are stable
#endif

    for(uint8_t cod=0;cod<coder_nb;cod++){

#ifdef CPLD
        uint32_t lines=gpio_get_all();                  // the result lines of this position, all read at the same instant
        if(__builtin_expect(cod+1<coder_nb,true)){
            gpio_put(CODER_CLK_CNT_PIN,1);              // rising edge: the CPLD moves to the next position
            busy_wait_at_least_cycles(4);
            gpio_put(CODER_CLK_CNT_PIN,0);              // the next result lines settle while this position is processed (at least 400 ns)
        }
#endif 

#ifndef CPLD
        gpio_put_masked(sel_gpio_mask, cod << gpio_sel0_pin);     // sel current coder ; env 6uS le pas de boucle + les traitements
        quick_delay(4);         // 9uS semble nécessaire pour stabiliser les coders et 4051 sinon ca fait nimporte quoi
                                // temps total du step 19uS ! avec 8mS d'intervalle ça semble ok (v1.2)
                                // mesure 2.5uS total avec le delay !!! incompréhensible ... et ça marche
        uint32_t lines=gpio_get_all();
#endif            

        CoderPhys* ph=&coderPhys[cod];  // physical configuration of this position
        uint8_t coder=ph->coder;        // logical number of the coder
        cp=&c[coder];
        uint8_t touch=ph->touch;        // logical number of the touch button
        ct=&c[touch];

        bool lineSw=(lines>>gpio_switch_pin)&1;
        bool lineTb=(lines>>MUXED_TOUCH_PIN)&1;
        bool lineCk=(lines>>gpio_clock_pin)&1;
        bool lineDt=(lines>>gpio_data_pin)&1;

#ifdef TEST_CODERS
        int16_t logIdx=-1;                              // entry written for this position, completed after the decoding
        uint8_t raw=(lineCk<<2)|(lineDt<<1)|lineSw;
        if(raw!=coderLogLast[cod]){
            coderLogLast[cod]=raw;
            uint16_t h=coderLogHead;
            if((uint16_t)(h-coderLogTail)<CODER_LOG_LEN){
                logIdx=h&(CODER_LOG_LEN-1);
                CoderLogEntry* e=&coderLog[logIdx];
                e->time=currTime;e->pos=cod;e->raw=raw;e->detent=0;
                e->value=(coderTimerCount!=nullptr) ? *(coderTimerCount+coder) : -1;
                coderLogHead=h+1;
            }
            else coderLogLost++;
        }
#endif

        // traitement switchs (avant les coders pour ne pas être zappé par les "continue")
        if(__builtin_expect((cp->coderSwitch!=lineSw)&&((currTime-cp->coderSwitchTime)>CODER_SW_STROBE_MS),false)){
                cp->coderSwitch=lineSw;
                cp->coderSwitchTime=currTime;
                if(coderTimerSwitch!=nullptr){
                    *(coderTimerSwitch+coder)=(lineSw==ph->sw) ? ON_SWITCH : !ON_SWITCH;   // sw column: level of a pressed switch
                }
        }
        
        // traitement touch buttons (avant les coders pour ne pas être zappé par les "continue")  
        if(__builtin_expect((ct->touchButton!=lineTb && (currTime-ct->touchButtonTime >CODER_SW_STROBE_MS)),false)){   // si la valeur "mémoire" est != de la valeur lue et time-out de debounce ok...

            ct->touchButtonTime=currTime;                                       // re-arm time_out
            touch_button_handler(touch,&(ct->touchButton),coderTouchB);         // n° coder physique, valeur "mémoire", le buffer des valeurs courantes lues
        }

        // decoding from the state of CK and DT, no debouncing needed:
        // a bounce gives a change then its reverse ; one state skipped between two polls is taken in the direction of the last move
        uint8_t q=ph->type;                             // changes of state per detent: 4 (CT_FULL) or 2 (CT_HALF) ; 0: no coder
        if(q==CT_NONE) continue;

        uint8_t st=(ph->dir==DIR_NORM) ? coderStateOf(lineCk,lineDt) : coderStateOf(lineDt,lineCk);
        uint8_t d=(st-cp->coderState)&3;
        if(__builtin_expect(d==0,true)) continue;       // no change

        if(currTime-cp->coderMoveTime>CODER_DIR_HOLD_MS) cp->coderDir=0;    // pause: direction of the last move forgotten
        cp->coderMoveTime=currTime;

        int8_t acc=cp->coderAcc;
        if(d==2){                                       // one state skipped between two polls
            if(cp->coderDir==1 || cp->coderDir==-1) acc+=2*cp->coderDir;        // moving: same direction as the last move
            else if(!CODER_IS_REST(st,q)){acc=0;cp->coderDir=CODER_DIR_SKIP;}   // full-cycle coder starting: direction given by the next move
            // else: a whole detent from rest, direction unknown: ignored
        }
        else{
            int8_t step=(d==1) ? 1 : -1;
            if(cp->coderDir==CODER_DIR_SKIP) acc=3*step;    // the move after a skip from rest gives its direction
            else acc+=step;
            cp->coderDir=step;
        }
        cp->coderState=st;

        int8_t detent=0;
        if(acc>=(int8_t)q){detent=1;acc-=q;}
        else if(acc<=-(int8_t)q){detent=-1;acc+=q;}
        if(CODER_IS_REST(st,q)){                        // rest position: count restarts from 0
            acc=0;
            if(q==CT_FULL) cp->coderDir=0;              // and the direction (half-cycle coders rest at every detent: direction kept)
        }
        cp->coderAcc=acc;

        if(detent!=0) coderCount(coder,coderStep(cp,coder,detent,currTime));
#ifdef TEST_CODERS
        if(logIdx>=0){
            coderLog[logIdx].detent=detent;
            coderLog[logIdx].value=(coderTimerCount!=nullptr) ? *(coderTimerCount+coder) : -1;
        }
#endif
    }
//gpio_put(TST_PIN,0);restore_interrupts(ints);
    return true;    // restart the timer
}

void coderInit(uint8_t ck,uint8_t data,uint8_t sw,uint8_t nb,uint16_t ctpi,uint8_t cstn){

    // ********************* doit absolument etre fait avant la mise en route du timer *******************
    coderTimerCount=nullptr;
    coderTimerSwitch=nullptr;
    coderCountMaxi=nullptr;

    gpio_clock_pin=ck;
    gpio_data_pin=data;
    gpio_switch_pin=sw;
#ifndef CPLD  

//,,,
    gpio_vcc_pin=CODER_GPIO_VCC;
    gpio_sel0_pin=CODER_PIO_SEL0;
    coder_sel_nb=CODER_SEL_NB;
    
    gpio_init(gpio_vcc_pin);gpio_set_dir(gpio_vcc_pin,GPIO_OUT);gpio_put(gpio_vcc_pin,1);
    sel_gpio_mask=0;
    for(int pin=gpio_sel0_pin;pin<gpio_sel0_pin+coder_sel_nb;pin++){
        gpio_init(pin);
        gpio_set_function(pin, GPIO_FUNC_SIO);
        sel_gpio_mask |=1u<<pin;
    }
    gpio_set_dir_out_masked(sel_gpio_mask);    

    printf("coders sel gpio mask:%X sel0_pin:%d sel_nb:%d\n",sel_gpio_mask,gpio_sel0_pin,coder_sel_nb);

#endif    

    coder_nb=nb;

    coderTimerPollingInterval=ctpi;
    coderStrobeNumber=cstn;

    gpio_init(gpio_data_pin);gpio_set_dir(gpio_data_pin,GPIO_IN);gpio_pull_up(gpio_data_pin);
    gpio_init(gpio_clock_pin);gpio_set_dir(gpio_clock_pin,GPIO_IN);gpio_pull_up(gpio_clock_pin);
    gpio_init(gpio_switch_pin);gpio_set_dir(gpio_switch_pin,GPIO_IN);gpio_pull_up(gpio_switch_pin);

#ifdef TEST_CODERS
    for(uint8_t i=0;i<CODER_NB;i++) coderLogLast[i]=0xFF;       // the first scan logs every position
#endif

    for(uint8_t i=0;i<CODER_NB;i++){                             // touch buttons and coders without position
        c[i].touchButton=0;
        c[i].touchButtonTime=0;                                  // init debouncer
        c[i].coderItStatus=0;
        c[i].coderAcc=0;
        c[i].coderDir=0;
        c[i].coderMoveTime=0;
        c[i].coderDetentTime=0;
        c[i].coderDetentDir=0;
    }

    sleep_ms(10);

#ifdef CPLD
    gpio_init(CODER_CLK_CNT_PIN);gpio_set_dir(CODER_CLK_CNT_PIN,GPIO_OUT);
    gpio_put(CODER_CLK_CNT_PIN,0);
    gpio_init(CODER_CLR_CNT_PIN);gpio_set_dir(CODER_CLR_CNT_PIN,GPIO_OUT);
    gpio_put(CODER_CLR_CNT_PIN,1);sleep_us(1);gpio_put(CODER_CLR_CNT_PIN,0);     // reset the CPLD counter to position 0
#endif

    for(uint8_t cod=0;cod<coder_nb;cod++){
#ifndef CPLD
        gpio_put_masked(sel_gpio_mask,cod<<gpio_sel0_pin);       // sel one coder
        sleep_us(10);
#endif 
#ifdef CPLD
        if(__builtin_expect(cod!=0,true)){
            gpio_put(CODER_CLK_CNT_PIN,1);              // rising edge: next position
            busy_wait_at_least_cycles(20);              // about 130 ns at 150 MHz, so the CPLD result lines are stable
            gpio_put(CODER_CLK_CNT_PIN,0);
        }
#endif 
        CoderPhys* ph=&coderPhys[cod];
        Coders* cp=&c[ph->coder];
        bool ck0=gpio_get(gpio_clock_pin);
        bool dt0=gpio_get(gpio_data_pin);
        bool sw0=gpio_get(gpio_switch_pin);
        cp->coderClock0=ck0;                                     // get clock
        cp->coderData0=dt0;                                      // get data
        cp->coderState=(ph->dir==DIR_NORM) ? coderStateOf(ck0,dt0) : coderStateOf(dt0,ck0);
        cp->coderSwitch=sw0;                                     // level at rest: no false change at start-up
        cp->coderSwitchTime=0;                                   // init debouncer
//printf(" -coder#%d init d:%d c:%d s:%d\n",ph->coder,dt0,ck0,sw0);
    }
}

uint32_t test_cnt=1;
void slow_coder_test(uint32_t ms){
    
    coderInit(CODER_GPIO_CLOCK,CODER_GPIO_DATA,CODER_GPIO_SW,CODER_NB,CODER_TIMER_POLLING_INTERVAL_MS,CODER_STROBE_NUMBER);
        //uint8_t selc=coder | 0x08;  // *********************************** force 4051 enable haut ******************** 

#ifndef CPLD 
    for(uint8_t coder=0;coder<coder_nb;coder++){       
        gpio_put_masked(sel_gpio_mask, coder << gpio_sel0_pin);     // sel current coder
        sleep_us(1);
#endif
#ifdef CPLD
    gpio_put(CODER_CLK_CNT_PIN,0);
    gpio_put(CODER_CLR_CNT_PIN,1);sleep_us(1);gpio_put(CODER_CLR_CNT_PIN,0);     // reset the CPLD counter to position 0

    for(uint8_t coder=0;coder<coder_nb;coder++){ 
        if(__builtin_expect(coder!=0,true)){
            gpio_put(CODER_CLK_CNT_PIN,1);              // rising edge: next position
            busy_wait_at_least_cycles(20);              // about 130 ns at 150 MHz, so the CPLD result lines are stable
            gpio_put(CODER_CLK_CNT_PIN,0);
        }
#endif      
    printf("coder:%d:%u Ck:%d Da:%d Sw:%d\n",coder,int_counter,gpio_get(CODER_GPIO_CLOCK),gpio_get(CODER_GPIO_DATA),gpio_get(CODER_GPIO_SW));
    sleep_ms(ms);//printf("%d\n",test_cnt++);
    }
}

void coderSetup(volatile int16_t* cTC,volatile bool* cTS,volatile bool* cTB,uint16_t* maxi,uint8_t nb){
    coderTimerCount=cTC;
    coderTimerSwitch=cTS;
    coderTouchB=cTB;
    coderCountMaxi=maxi;
    if(coderTimerSwitch!=nullptr){
        for(uint8_t cod=0;cod<coder_nb;cod++){                                    // states read by coderInit
            uint8_t coder=coderPhys[cod].coder;
            *(coderTimerSwitch+coder)=(c[coder].coderSwitch==coderPhys[cod].sw) ? ON_SWITCH : !ON_SWITCH;
        }
    }
    //coder_nb=nb;
}