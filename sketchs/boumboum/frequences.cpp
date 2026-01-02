#include <stdio.h>
#include "pico/stdlib.h"
#include "frequences.h"
#include "util.h"
#include "const.h"
#include <math.h>

const uint8_t octNb = OCTNB;
float baseFreq = FREQ0;
float octFreq[octNb+1];
int16_t sineWaveform[BASIC_WAVE_TABLE_LEN];
int16_t squareWaveform[BASIC_WAVE_TABLE_LEN];
int16_t triangleWaveform[BASIC_WAVE_TABLE_LEN];
int16_t sawtoothWaveform[BASIC_WAVE_TABLE_LEN];
int16_t pinkNoiseWaveform[BASIC_WAVE_TABLE_LEN];
int16_t whiteNoiseWaveform[BASIC_WAVE_TABLE_LEN];

const uint16_t octIncrNb = 409;
float octIncr[octIncrNb];


void fillBasicWaveForms(){
    printf("  filling basic \n");
    for(uint16_t i=0;i<BASIC_WAVE_TABLE_LEN/4;i++){
        sineWaveform[i]= (uint16_t)(sin(((float)i)/BASIC_WAVE_TABLE_LEN*2*PI)*MAX_AMP_VAL);
        sineWaveform[(BASIC_WAVE_TABLE_LEN/2)-i]=sineWaveform[i];
        sineWaveform[i+(BASIC_WAVE_TABLE_LEN/2)]=-sineWaveform[i];
        sineWaveform[BASIC_WAVE_TABLE_LEN-i]=-sineWaveform[i];
        //printf("%d %5.4f %8x \n",i,sin(((float)i)/SINE_WAVE_TABLE_LEN*2*PI),sineWaveform[i]);
       /* squareWaveform[i]=MAX_AMP_VAL;
        squareWaveform[i+(BASIC_WAVE_TABLE_LEN/4)]=MAX_AMP_VAL;
        squareWaveform[i+(BASIC_WAVE_TABLE_LEN/2)]=-MAX_AMP_VAL;
        squareWaveform[BASIC_WAVE_TABLE_LEN-i]=-MAX_AMP_VAL;

        triangleWaveform[i]=i*(MAX_AMP_VAL/(BASIC_WAVE_TABLE_LEN/4));
        triangleWaveform[(BASIC_WAVE_TABLE_LEN/2)-i]=triangleWaveform[i];
        triangleWaveform[i+(BASIC_WAVE_TABLE_LEN/2)]=-triangleWaveform[i];
        triangleWaveform[BASIC_WAVE_TABLE_LEN-i]=-triangleWaveform[i];

        sawtoothWaveform[i]=i*MAX_AMP_VAL/BASIC_WAVE_TABLE_LEN/2;
        sawtoothWaveform[(BASIC_WAVE_TABLE_LEN/4)+i]=sawtoothWaveform[i]+MAX_AMP_VAL/2;
        sawtoothWaveform[(BASIC_WAVE_TABLE_LEN/2)+i]=-(MAX_AMP_VAL-sawtoothWaveform[i]);
        sawtoothWaveform[BASIC_WAVE_TABLE_LEN-i]=-sawtoothWaveform[i];        
*/
    }
}

// tableau des fréquences d'octaves
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

// tableau des ratios d'incréments sur 1 octave
void fillOctIncr() 
{ 
  for (uint16_t i = 0; i < octIncrNb; i++) {
    octIncr[i] = (float)(powf((float)2,(float)i/(float)octIncrNb))-1; 
  }
}                        

void showOctIncr(float oct0,float octn)
{ 
  for(uint8_t j=oct0;j<octn;j++){

    printf("  filling ratios d'incréments d'octave oct:%d f:%4.3f à f:%4.3f\n",j,octFreq[j],octFreq[j+1]);
    uint8_t dec=4;
    uint8_t step=4;
    uint16_t max=octIncrNb/step*step;
    float delta=octFreq[j+1]-octFreq[j];
  
    for (uint16_t i = 0; i < max; i+=step ){
      if(i==0 || i==max-step){
        printf("octIncr[%03d] = %0.4f=%5.3f  %0.4f=%5.3f %0.4f=%5.3f %0.4f=%5.3f\n",i,
          octIncr[i],delta*octIncr[i]+octFreq[j],
          octIncr[i+1],delta*octIncr[i+1]+octFreq[j],
          octIncr[i+2],delta*octIncr[i+2]+octFreq[j],
          octIncr[i+3],delta*octIncr[i+3]+octFreq[j]);
      }
    }
  }
}

// calcul de la fréquence sonore à partir de la valeur linéaire
float calcFreq(uint16_t val) // from lin value (0-octIncrNb*OCTNB) to snd value (baseF à baseF*2^OCTNB)
{ 
  uint8_t oct = val/ octIncrNb;
  uint16_t incr = val % octIncrNb;
  float freq = octFreq[oct] +octIncr[incr]*(octFreq[oct+1]-octFreq[oct]);
 return freq;
}

// initialisation des tableaux pour permettre calcFreq()
void freq_start()                //void setup() 
{  
  //Serial.begin(115200);
  printf(" -calcul fréquences\n");
  
  fillOctFreq();
  showOctFreq();
  fillOctIncr();
  showOctIncr(0,1);

}

void voiceInit(float freq,struct voice* v)
{
    v->sampleNbToFill=SAMPLE_BUFFER_SIZE;    
    v->currentSample=0;
    v->frequency=freq;    
    for(uint8_t i=0;i<BASIC_WAVES_NB;i++){v->basicWaveAmpl[i]=0;}  // all waves off
    v->freqCoeff=0;
    v->dhexFreq=0;
    v->moduloMask=0;
    v->moduloShift=0;
    
    uint32_t fr=(uint16_t)freq;
    v->freqCoeff=32;
    while(fr!=0){                   
        fr>>=1;v->freqCoeff--;      // @16KHz maxi, coeff min=18 ; @16hz mini, coeff max=27
    }

    v->dhexFreq=(uint32_t)(freq*(1<<v->freqCoeff))/SAMPLE_RATE;

    v->moduloMask=(1<<(v->freqCoeff+1))-1;

    v->moduloShift=v->freqCoeff-BASIC_WAVE_TABLE_POW;

    printf("voice init freq:%5.2f freqCoeff:%d dhexFreq:%08x moduloMask:%08x moduloShift:%d\n",
        v->frequency,v->freqCoeff,v->dhexFreq,v->moduloMask,v->moduloShift);
}

void fillVoiceBuffer(int32_t* sampleBuffer,struct voice* v)
{
  gpio_put(TEST_PIN,ON);

  uint32_t ech; 
  for(uint16_t i=0;i<v->sampleNbToFill;i++){
    float int_part;

    ech=(uint32_t)(modff(v->currentSample*v->frequency/SAMPLE_RATE,&int_part)*BASIC_WAVE_TABLE_LEN); // ech nbr 

    sampleBuffer[i*2]=sineWaveform[ech]*v->genAmpl;
    /*
        ((sineWaveform[ech]*v->basicWaveAmpl[WAVE_SINUS]
      + squareWaveform[ech]*v->basicWaveAmpl[WAVE_SQUARE]
      + triangleWaveform[ech]*v->basicWaveAmpl[WAVE_TRIANGLE]
      + sawtoothWaveform[ech]*v->basicWaveAmpl[WAVE_SAWTOOTH]
      + whiteNoiseWaveform[ech]*v->basicWaveAmpl[WAVE_WHITENOISE]
      + pinkNoiseWaveform[ech]*v->basicWaveAmpl[WAVE_PINKNOISE])
      /16)*v->genAmpl;
      */
    sampleBuffer[i*2+1]=sampleBuffer[i*2]; // stereo

    v->currentSample++;
  }
  if(v->currentSample>=SAMPLE_RATE*10 && ech>=BASIC_WAVE_TABLE_LEN-4){v->currentSample=0;}   // re-init to avoid phase error

  gpio_put(TEST_PIN,OFF);
  
}

