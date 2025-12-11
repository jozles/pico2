#ifndef _I2S_H_
#define _I2S_H_

/* comment ça fonctionne 
    depuis le programme principal on fait i2s_start() pour tout initialiser et démarrer 
    puis remplir un buffer, préciser son adresse dans audio_data 
    et recommencer à chaque fois que i2s_hungry devient true 
    SAMPLES_PER_BUFFER indique le nombre d'échantillons (32bits) par canal L/R du buffer
    */
 

void bb_i2s_start();

#endif //_I2S_H_