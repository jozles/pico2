#include "pico/stdlib.h"
#include "hardware/timer.h"

#define LED_PIN 25   // LED intégrée sur le Pico 2

uint32_t cnt=0;
uint32_t led_sta=true;


// Callback appelé par l'interruption du timer
bool repeating_timer_callback(struct repeating_timer *t) {
    
    cnt++;
    if(cnt>=10){cnt=0;led_sta=!led_sta;}
    
    if(led_sta){
        static bool led_state = false;
        led_state = !led_state;
        gpio_put(LED_PIN, led_state);   // bascule la LED
    }        
    return true; // relancer le timer
}

int main() {
    stdio_init_all();

    // Configure la LED en sortie
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    // Crée un timer répétitif toutes les 500 ms
    struct repeating_timer timer;
    add_repeating_timer_ms(500, repeating_timer_callback, NULL, &timer);

    // Boucle principale vide : tout est géré par l’interruption
    while (true) {
        tight_loop_contents(); // évite l’optimisation
    }
}
