#ifndef _SND_LEV_H_
#define _SND_LEV_H_


void ampGlideTick();                                // glide of the coder levels: call it from an interrupt running at 1 kHz or faster

void fillAmplIncr();
void setVoiceAmplCoders(Voice* v, uint8_t item, uint16_t coder, uint16_t coderAtt);   // load the coders of a wave (item 0..5) or of the voice gain (item 6), then compute the level
void setVoiceAmpl(Voice* v, uint8_t item);                                            // compute only, from the stored coders and the stored value of the input
uint16_t voiceAmplCoder(const Voice* v, uint8_t item);                                // coder of a wave or of the voice gain
uint16_t voiceAmplAtt(const Voice* v, uint8_t item); 
void ampGlideTick();                                                                  // glide of the coder levels: call it from an interrupt running at 1 kHz or faster



#endif  // _SND_LEV_H_