#ifndef _INPUTS_TB_MNGT_H_
#define _INPUTS_TB_MNGT_H_

typedef struct {
    int16_t  valeur;        // valeur d'entrée
    uint8_t  type_norm;     // type de normalisation
    int16_t  next_input_id; // index suivant, -1 = fin
} Input_lnk;

#endif // _INPUTS_TB_MNGT_H_