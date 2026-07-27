#ifndef _SND_LEV_H_
#define _SND_LEV_H_

//uint16_t getAmpl(Voice* v,uint8_t wav);
void fillAmplIncr();
void setVoicesAmpl(uint8_t v,uint8_t item,int32_t valeur);
void setVoicesAmpl(uint8_t v,uint8_t item);

void setVoicesFreqAtt(uint8_t v,uint32_t coderF);
void setLfosFreqAtt(uint8_t l,uint32_t coderF);
void setLfosFreq(uint8_t l,uint32_t coderF);
void setLfosFrParams(uint8_t l);

#endif  // _SND_LEV_H_