#include <stdio.h>
#include "pico/stdlib.h"
#include "frequences.h"
#include "util.h"
#include "const.h"
#include <math.h>

const uint8_t octNb = OCTNB;
float baseFreq = FREQ0;
float octFreq[octNb+1];
int16_t sineWaveform[WFSTEPNB];

const uint16_t octIncrNb = 409;
float octIncr[octIncrNb];

void fillSineWaveForms(){

    //sine_wave_table[i] = 32767 * cosf(i * 2 * (float) (M_PI / SINE_WAVE_TABLE_LEN));

    printf("  filling sinus \n");
    for(uint16_t i=0;i<WFSTEPNB/4;i++){
        sineWaveform[i]= (uint16_t)(sin(((float)i)/WFSTEPNB*2*PI)*32767);
        sineWaveform[1023-i]=sineWaveform[i];
        sineWaveform[i+1024]=-sineWaveform[i];
        sineWaveform[2047-i]=-sineWaveform[i];
        //printf("%d %5.4f %8x \n",i,sin(((float)i)/WFSTEPNB*2*PI),sineWaveform[i]);
    }
}

void fillOctFreq() { 
  for (uint8_t i = 0; i <= octNb; i++) {
    octFreq[i] = baseFreq * (1<<i); 
  }
}

void showOctFreq() 
{ 
  printf("  fréquences des octaves\n");
  for (uint16_t i = 0; i < octNb; i++ ){
    printf("%d: %5.3f - ",i,octFreq[i]);
  }
  printf("\n");
}

void fillOctIncr() 
{ 
  for (uint16_t i = 0; i < octIncrNb; i++) {
    octIncr[i] = (float)(powf((float)2,(float)i/(float)octIncrNb))-1; 
  }
}

void fout(float octF,float octF_1,uint16_t incr,uint8_t dec) 
{ 
  printf("%5.4f/%5.4f ",octIncr[incr],octF+(octF_1-octF)*octIncr[incr]);
}                         

void showOctIncr(float oct0,float octn)
{ 
for(uint8_t j=oct0;j<octn;j++){

  printf("  ratios d'incréments d'octave oct:%d f:%4.3f à f:%4.3f\n",j,octFreq[j],octFreq[j+1]);
  uint8_t dec=4;
  uint8_t step=4;
  uint16_t max=octIncrNb/step*step;
  float delta=octFreq[j+1]-octFreq[j];
  
  for (uint16_t i = 0; i < max; i+=step ){
    printf("octIncr[%d] = %0.4f=%5.3f  %0.4f=%5.3f %0.4f=%5.3f %0.4f=%5.3f\n",i,
      octIncr[i],delta*octIncr[i]+octFreq[j],
      octIncr[i+1],delta*octIncr[i+1]+octFreq[j],
      octIncr[i+2],delta*octIncr[i+2]+octFreq[j],
      octIncr[i+3],delta*octIncr[i+3]+octFreq[j]);
  }
}
  
}

float calcFreq(uint16_t val) // from lin value (0-octIncrNb*OCTNB) to snd value (baseF à baseF*2^OCTNB)
{ 
  uint8_t oct = val/ octIncrNb;
  uint16_t incr = val % octIncrNb;
  float freq = octFreq[oct] +octIncr[incr]*(octFreq[oct+1]-octFreq[oct]);
 return freq;
}

void freq_start()                //void setup() 
{  
  //Serial.begin(115200);
  printf(" -calcul fréquences\n");
  
  fillOctFreq();
  showOctFreq();
  fillOctIncr();
  showOctIncr(0,1);
  

  //showOctIncr(octFreq[oct],octFreq[oct+1]);
}

//void loop(){
//blink_wait();
//}