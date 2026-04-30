#ifndef _INPUTS_TB_MNGT_H_
#define _INPUTS_TB_MNGT_H_

bool init_objects_output_ptr(void);
bool init_objects_input_ptr(void);
void connect_input(uint16_t id, uint16_t output, uint8_t type_norm);
void disconnect_input(uint16_t id, uint16_t output);
void update_inputs(uint16_t output,int16_t valeur);
void in_table_init();

#endif // _INPUTS_TB_MNGT_H_