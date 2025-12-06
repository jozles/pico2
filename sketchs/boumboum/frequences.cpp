#include <stdio.h>
#include "pico/stdlib.h"
#include "frequences.h"
#include "util.h"
#include "const.h"
#include <math.h>

const uint8_t octNb = OCTNB;
float baseFreq = FREQ0;
float octFreq[octNb+1];

void fillOctFreq() { 
  for (uint8_t i = 0; i <= octNb; i++) {
    octFreq[i] = baseFreq * (1<<i); 
  }
}

void showOctFreq() 
{ 
  printf("fréquences des octaves");
  for (uint16_t i = 0; i < octNb; i++ ){
    printf("%5.4f",octFreq[i]);
  }
}

const uint16_t octIncrNb = 409;
float octIncr[octIncrNb];

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

void showOctIncr(float octF,float octF1)
{ 
  printf("ratios d'incréments d'octave\n");
  uint8_t dec=4;
  uint8_t step=4;
  uint16_t max=octIncrNb/step*step;
  for (uint16_t i = 0; i < max; i+=step ){
    for(uint8_t j=0;j<step;j++){fout(octF,octF1,i+j,dec);} 
    printf("\n");
  }
  if(max<octIncrNb){
    for (uint16_t i = max+1; i < octIncrNb; i++ ){
      fout(octF,octF1,i,dec);
    }
    printf("\n---");
  }
}

float calcFreq(uint16_t val) 
{ 
  uint8_t oct = val / octIncrNb;
  uint16_t incr = val % octIncrNb;
  float freq = octFreq[oct] * octIncr[incr];
 return freq;
}

void start()                //void setup() 
{  
  //Serial.begin(115200);
  printf("+calcul fréquences\n");
  
  fillOctFreq();
  showOctFreq();
  fillOctIncr();
  showOctIncr(0,0);
  
  uint8_t oct = 4;
  printf("Exemples de fréquences (octave %d)\n",oct);

  showOctIncr(octFreq[oct],octFreq[oct+1]);
}

//void loop(){
//blink_wait();
//}