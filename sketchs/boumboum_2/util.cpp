#include "pico/stdlib.h"
#include "const.h"

uint32_t millisTimer;
uint32_t ledsTimer;
uint32_t ledOnTime=LEDONDURAT;
uint32_t ledOffTime=LEDOFFDURAT;

void ledBlink(){
    if(gpio_get(LED)){
        if((millisTimer-ledsTimer)>ledOnTime){gpio_put(LED,LOW);}
    }
    else {
        if((millisTimer-ledsTimer)>ledOffTime){gpio_put(LED,HIGH);}
    }
}

void blink_wait(){
    uint32_t ledOnTimeBackup=ledOnTime;ledOnTime=800;
    uint32_t ledOffTimeBackup=ledOffTime;ledOffTime=800;

  while(1){
    
    ledBlink();

    //if (Serial.available()) break;} // wait for Serial to be ready  
  }           

    uint32_t ledOnTime=ledOnTimeBackup;
    uint32_t ledOffTime=ledOffTimeBackup;
}

bool millisTimerHandler(struct repeating_timer *t){
    millisTimer++;
    return true;
}

void initLeds(){
    gpio_set_dir(LED,GPIO_OUT); gpio_put(LED,LOW);
    gpio_set_dir(LED_BLUE,GPIO_OUT);gpio_put(LED_BLUE,LOW);
    gpio_set_dir(LED_RED,GPIO_OUT);gpio_put(LED_RED,LOW);
    gpio_set_dir(LED_GREEN,GPIO_OUT);gpio_put(LED_GREEN,LOW);
    gpio_set_dir(LED_GND,GPIO_OUT);gpio_put(LED_GND,LOW);

    struct repeating_timer millisTimer;
    add_repeating_timer_ms(1, millisTimerHandler, NULL, &millisTimer);
}