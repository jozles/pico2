#ifndef _INPUTS_TB_MNGT_H_
#define _INPUTS_TB_MNGT_H_


bool init_objects_outputs(void);
bool init_objects_inputs(void);
void connect_input(uint16_t input_id, uint16_t output);
void disconnect_input(uint16_t input_id, uint16_t output);
void update_inputs(uint16_t output,int16_t valeur);
void objects_table_init();

#endif // _INPUTS_TB_MNGT_H_