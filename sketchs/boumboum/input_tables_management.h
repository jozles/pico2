#ifndef _INPUTS_TB_MNGT_H_
#define _INPUTS_TB_MNGT_H_


bool init_objects_outputs(void);
bool init_objects_inputs(void);
void connect_input(uint16_t input_id, uint16_t output);
void disconnect_input(uint16_t input_id, uint16_t output);
void update_inputs(uint8_t src,int16_t input_id,int16_t valeur);
void objects_table_init();

// (0 no trig ; 1 up ; 2 down ; 3 both)
enum Inputs_trig_modes {
    INP_NO_TRIG,
    INP_UP_TRIG,
    INP_DOWN_TRIG,
    INP_U_D_TRIG,
    INP_TRIG_ST_NB
};

#endif // _INPUTS_TB_MNGT_H_