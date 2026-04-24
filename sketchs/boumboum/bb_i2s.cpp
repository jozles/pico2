/* bb_i2s.cpp */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/sync.h"

#include "i2s.pio.h"
#include "const.h"

extern volatile uint32_t millisCounter;

volatile bool i2s_buf_free[2]; // false busy : buffer ready for dma

int i2s_dma_chan0;
int i2s_dma_chan1;
static dma_channel_config dma_cfg0;
static dma_channel_config dma_cfg1;

__attribute__((aligned(8)))
int32_t* i2s_buffer[2];

static PIO i2s_pio;
static int i2s_sm;


void __not_in_flash_func(dma_i2s_handler)() {
    uint32_t status = dma_hw->ints0;    //dma_hw->intr;

    if (status & (1u << i2s_dma_chan0)) {
        i2s_buf_free[0]=true;
        dma_hw->ints0 = (1u << i2s_dma_chan0);
    }
    if (status & (1u << i2s_dma_chan1)) {
        i2s_buf_free[1]=true;
        dma_hw->ints0 = (1u << i2s_dma_chan1);
    }
}


int init_dma_i2s() {
    i2s_dma_chan0 = dma_claim_unused_channel(true);
    if(i2s_dma_chan0<0){return -1;}                     // no channel available           

    i2s_dma_chan1 = dma_claim_unused_channel(true);
    if(i2s_dma_chan1<0){return -2;}                     // no channel available

    dma_cfg0 = dma_channel_get_default_config(i2s_dma_chan0);
    dma_cfg1 = dma_channel_get_default_config(i2s_dma_chan1);    

    channel_config_set_transfer_data_size(&dma_cfg0, DMA_SIZE_32);
    channel_config_set_transfer_data_size(&dma_cfg1, DMA_SIZE_32);

    channel_config_set_read_increment(&dma_cfg0, true);
    channel_config_set_read_increment(&dma_cfg1, true);    

    channel_config_set_write_increment(&dma_cfg0, false);
    channel_config_set_write_increment(&dma_cfg1, false);    

    channel_config_set_dreq(&dma_cfg0,pio_get_dreq(i2s_pio, i2s_sm, true));                   // voir commentaire dans main
    channel_config_set_dreq(&dma_cfg1,pio_get_dreq(i2s_pio, i2s_sm, true));                   // voir commentaire dans main    

    channel_config_set_chain_to(&dma_cfg0, i2s_dma_chan1);
    channel_config_set_chain_to(&dma_cfg1, i2s_dma_chan0);

    dma_channel_set_irq0_enabled(i2s_dma_chan0, true);
    dma_channel_set_irq0_enabled(i2s_dma_chan1, true);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_i2s_handler);
    irq_set_enabled(DMA_IRQ_0, true);

    return 1;
}

void i2s_start(){

    dma_channel_configure(i2s_dma_chan0, &dma_cfg0,&i2s_pio->txf[i2s_sm], i2s_buffer[0], SAMPLE_BUFFER_SIZE*2,false);
    dma_channel_configure(i2s_dma_chan1, &dma_cfg1,&i2s_pio->txf[i2s_sm], i2s_buffer[1], SAMPLE_BUFFER_SIZE*2,false);
    dma_start_channel_mask(1u << i2s_dma_chan0); // seulement chan0
}

int i2sSetup(PIO pio,uint8_t i2sDataPin,int32_t* buf[2]) {

    i2s_pio=pio;
    i2s_buffer[0]=buf[0];
    i2s_buffer[1]=buf[1];

    // get sm
    i2s_sm = pio_claim_unused_sm(pio, true); 
    if(i2s_sm<0){printf("i2sSetup: no sm available\n");return -3;}

    printf("i2sSetup pio:%d sm:%d\n",pio_get_index(pio),i2s_sm);
    uint offset = pio_add_program(i2s_pio, &i2s_program);

    // sm config
    pio_gpio_init(i2s_pio, i2sDataPin);                                       // attache le gpio au pio (si plusieurs gpio plusieurs inits)
    pio_gpio_init(i2s_pio, i2sDataPin+1);                                     // bclk
    pio_gpio_init(i2s_pio, i2sDataPin+2);                                     // lrclk
    pio_sm_set_consecutive_pindirs(i2s_pio, i2s_sm, i2sDataPin, 3, true);     // 1er,nbre,direction des gpio de la sm (correspond pour le pilotage sm à "gpio_set_dir()" en pilotage processeur)

    pio_sm_config c = i2s_program_get_default_config(offset);       // créé la structure de la config de la sm
    sm_config_set_sideset_pins(&c, i2sDataPin+1);                   // gpio de base de la sm qui sera associée à la structure                 
    sm_config_set_out_pins(&c, i2sDataPin, 1);                      // direction des GPIOs de la sm (pas compris pourquoi il y a 2 couches de direction avec pio_sm_consecutive_pindirs)                 
    sm_config_set_out_shift(&c, false, true, 32);                   // controle du shift register alimenté par le TX FIFO (,right,autopull,threshold)
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);                  // concatène FIFO TX et RX (8 bytes)
   
    sm_config_set_clkdiv(&c, 5.2816f);                              // (31*10+1*12)*2=644 ; 150000/44.1/644=5.2816  voir i2s.pio
    pio_sm_init(i2s_pio, i2s_sm, offset, &c);                       // attache le programme et la structure à la sm                 
    
    pio_sm_clear_fifos(i2s_pio, i2s_sm);
    pio_sm_set_enabled(i2s_pio, i2s_sm, true);

    // dma init
    int i2s_dma_channel=init_dma_i2s();
    if(i2s_dma_channel<0){printf("i2sSetup: no dma channel available\n");return i2s_dma_channel;}   // -1 ou -2

    i2s_buf_free[0]=true;
    i2s_buf_free[1]=true;

    printf("début i2s dma0:%i dma1:%i\n",i2s_dma_chan0,i2s_dma_chan1);
    return 0;
}

int i2s_active_dma(){
    if(dma_channel_is_busy(i2s_dma_chan0)){return 0;};
    if(dma_channel_is_busy(i2s_dma_chan1)){return 1;};
    return -1;
}
