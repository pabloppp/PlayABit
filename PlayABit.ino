
#include <SPI.h>
#include <MIDI.h>

#include "drumkit.h"

MIDI_CREATE_DEFAULT_INSTANCE();

const int notes[] = {
  2093, 2217, 2349,    // other octaves by dividing by power of 2
  2489, 2637, 2794, 2960,   //C7 - B7
  3136, 3322, 3520, 3729, 3951
};

#define PULSE 0
#define SAW 1
#define TRI 2
#define NOISE 3

#define PRESS 1
#define RELEASE 0

struct oscillatorStruct {
  volatile unsigned int ticks;
  volatile unsigned int phase;
  volatile byte waveform;  //0:square 1:saw 2:triangle  3:noise
  volatile byte dutyCycle; //only for square
  volatile byte volume;
};

struct oscillatorStruct oscillators[8];

struct channelStruct {
  volatile byte note;  //done
  volatile byte glissandoRate;  //done
  volatile byte glissando;  //done
  volatile unsigned int vibratoPhase; 
  volatile byte arpegioTimer; //done
  volatile boolean burst;
  volatile byte instrument;
};

struct channelStruct channels[8];

struct keyStruct {
  volatile boolean pressed;
  volatile byte channel;
  volatile boolean update;
};

struct keyStruct keys[128];

byte lastChannel = 0;

// volatile variables used in ISR
volatile int outputA = 0;


//not volatile variables
byte arpegioTimer = 0;
byte arpegioDelay = 4;  //if another key is pressed within 0.2s start arpedio mode
boolean arpegioMode = false;
boolean glissandoMode = true;
boolean burst = false;
byte releaseRate = 0;      // 1 = 2,5 secs
unsigned int vibratoTics = 4  * 65535UL/100;  // 1 = 1Hz 
byte vibratoAmp = 8;
boolean started = false;
unsigned int trackTics = 4  * 65535UL/100;  // frequency of a track (there are 8 grups of 8 tracks) must be a divisor of 100
unsigned int trackPhase = 0;
byte trackCount = 0;
boolean trackOn;

boolean recording = true;
boolean replaying = false;
byte loopTables[8][8][4];


byte globalInstrument = PULSE;
byte globalDutyCycle = 127; //50%: 127   25%: 63  12.5%: 31

void setup(){    
  setupTimer();
  
  pinMode(13, OUTPUT);  //LED 13
  arpegioTimer = 0;
  
  //start melody
  oscillators[0].dutyCycle = 127;
  oscillators[0].volume = 50;
  oscillators[0].waveform = PULSE;
  oscillators[1].waveform = NOISE;

  setFreq(0, midi2Freq( 88 ));
  delay(100);
  setFreq(0, midi2Freq( 100 ));
  for(int i=50;i>=0;i--){
    oscillators[0].volume = i;
    delay(20);
  }


  MIDI.begin(1);
  
  for(int i=0;i<8;i++){
    channels[i].note = 0;
    channels[i].glissandoRate = 1;
    channels[i].glissando = 0;
    channels[i].vibratoPhase = 0;
    channels[i].instrument = globalInstrument;
    
    oscillators[i].dutyCycle = globalDutyCycle;  
    oscillators[i].volume = 0;
    oscillators[i].waveform = TRI;
  }
  
  for(int i=0;i<127;i++){
    keys[i].pressed = false;
    keys[i].channel = 0;
    keys[i].update = false;
  }
  
  started = true;
  
}

void loop(){ 
  
    while(MIDI.read()){
      byte note = MIDI.getData1();
      byte data2 = MIDI.getData2();
      switch(MIDI.getType()){
        case midi::NoteOn:
          playNote(PRESS, note, globalInstrument);
          if(recording){
            byte noteEvent[4] = {true, PRESS, note, globalInstrument};
            for(int i=0;i<8;i++){
              if(loopTables[trackCount][i][0] == false){
                loopTables[trackCount][i][0] = true; loopTables[trackCount][i][1] = PRESS; loopTables[trackCount][i][2] = note; loopTables[trackCount][i][3] = globalInstrument;
                break;
              }
            }
          }
          break;
        
        case midi::NoteOff:
          playNote(RELEASE, note, globalInstrument);
          if(recording){
            byte noteEvent[4] = {true, RELEASE, note, globalInstrument};
            for(int i=0;i<8;i++){
              if(loopTables[trackCount][i][0] == false){
                loopTables[trackCount][i][0] = true; loopTables[trackCount][i][1] = RELEASE; loopTables[trackCount][i][2] = note; loopTables[trackCount][i][3] = globalInstrument;
                break;
              }
            }
          }
          break;
        
        default:
          break;  
      }
    }
    
    //arpegio
    for(int i=0;i<8;i++){
      
      if(channels[i].arpegioTimer == 0){
        channels[i].arpegioTimer = arpegioDelay;
        
        byte newNote = channels[i].note-1;
        for(int j=0;j<127;j++){  //recorremos todas las notas a ver si alguna esta asociada al canal 1
          newNote &= ~(1 << 7);
          if(keys[newNote].pressed == true && keys[newNote].channel == i && channels[i].note != newNote){
            channels[i].note = newNote;
            keys[newNote].update = true;

            break;
          }
          newNote -= 1;
        }
        
      }
      
    }
    //~~
    
    
    //READING VALUES FROM POTENTIMETER
    int waveVal = analogRead(A0);
    if(waveVal < 342) globalInstrument = PULSE;
    else if(waveVal < 683) globalInstrument = TRI;
    else if(waveVal < 1024) globalInstrument = SAW;
    //else if(waveVal < 1024) globalInstrument = NOISE;
        
}

void playNote(boolean pressed, byte data1, byte instrument){
  if(pressed){
    for(int i=0;i<8;i++){
      if(glissandoMode && arpegioTimer > 0 && data1-1 == channels[lastChannel].note){ //if note pressed in glissando mode
        byte g = midi2Freq(data1) - midi2Freq(channels[lastChannel].note);
        channels[lastChannel].glissando = g;
        channels[lastChannel].note = data1;
        keys[data1].channel = lastChannel;
        keys[data1].pressed = true;
        keys[data1-1].pressed = false;
        keys[data1].update = true;
        keys[data1-1].update = true;
        break;  
      }
      else if(arpegioMode && arpegioTimer > 0){  //else if note pressed in arpegio mode
        keys[data1].channel = lastChannel;
        keys[data1].pressed = true;
        break;
      }
      else if(keys[channels[i].note].pressed == false){  //else normal note
        channels[i].note = data1;
        channels[i].instrument = instrument;
        lastChannel = i;
        keys[data1].channel = lastChannel;
        keys[data1].pressed = true;
        keys[data1].update = true;
        if(burst) channels[i].burst = true;
        break;
      }
    }
    arpegioTimer = arpegioDelay;
  }
  else{
    keys[data1].pressed = false;
    channels[keys[data1].channel].note = 0;
  }
}

void playBackNote(boolean pressed, byte data1, byte instrument, byte channel){
  if(pressed){
    channels[channel].note = data1;
    channels[channel].instrument = instrument;
    channels[channel].note = data1;
    keys[data1].pressed = true;
    keys[data1].update = true;
  }
  else{
     keys[data1].pressed = false;
     channels[channel].note = 0;
  }
}

void setupTimer(){
  
  pinMode(9, OUTPUT);  //set OC1A pin to output
  pinMode(10, OUTPUT);  //set OC1B pin to output
  
  cli(); //stop all interrupts
  
  //iniciamos los registros a 0
  TCCR1A = 0;
  TCCR1B = 0;
  
  //para el modo FAST PWM ponemos los bits 
  //WGM10: 0; WGM11: 1; WGM12: 1; WGM13: 1;
  //al poner WGM10 a 0 utilizaremos ICR1
  
  TCCR1A |= 1 << WGM11;
  TCCR1B |= 1 << WGM12;
  
  //prescalar 1 (001) CS12 CS11 CS10
  TCCR1B |= 1 << CS10;
      
  TCCR1A |= 1 << COM1A1;
  TCCR1A |= 1 << COM1B1;
  
  //ponemos los counters con el valor adecuado
  ICR1 = 511;
  OCR1A = 0;
  OCR1B = 0;
  
  //activamos el overflow interrupr del time 1 para que se ejecute 
  //la funcion que queremos
  TIMSK1 |= 1 << TOIE1;
  
  //~~~~TIMER 2 100Hz
  TCCR2A = 0;
  TCCR2B = 0;
  
  TCNT2 = 0; //initial counter value (99)
  OCR2A = 156; //156*100 = 16000000/1024
  
  TCCR2A |= 1 << WGM21;  //CTC MODE
  TCCR2B |= 1 << CS20; //Prescaler 1024
  TCCR2B |= 1 << CS21;
  TCCR2B |= 1 << CS22;
  
  TIMSK2 |= 1 << OCIE2A; //interrupt en el match
  
  sei(); //start all interrupts
  
}

ISR (TIMER2_COMPA_vect)  {
  
  if(!started) return;
  //Timer 2 called at 100Hz
  
  
  //LOOPS
  int trackTime = trackPhase >> 8;
  
  if(trackTime >= 128 && !trackOn){   
    trackOn = true;
    
    
    //replay loop
    if(replaying){
      for(int i=0;i<8;i++){
        if(loopTables[trackCount][i][0] == true){
          playBackNote(loopTables[trackCount][i][1], loopTables[trackCount][i][2], loopTables[trackCount][i][3], i);
        }
      }
    }
    
    trackCount++;
    if(trackCount%2 == 0){
      digitalWrite(13, HIGH);
      //playBackNote(PRESS, 60, PULSE, 7);
    }
    if(trackCount >= 8){
      trackCount = 0;
    }
  } 
  else if(trackTime < 128 && trackOn){
    digitalWrite(13, LOW);
    //playBackNote(RELEASE, 50, PULSE, 7);
    trackOn = false;  
  }

  trackPhase += trackTics;
  // END OF LOOPS
  
  if(arpegioTimer > 0){
    arpegioTimer--;
  }

  
  for(int i=0;i<8;i++){
    
    //arpegio update
    if(arpegioMode && channels[i].arpegioTimer > 0){
      channels[i].arpegioTimer--;
    }
    
    //glissando update
    if(glissandoMode && channels[i].glissando > 0){       
      keys[channels[i].note].update = true;
      if(channels[i].glissando <= channels[i].glissandoRate) channels[i].glissando = 0;
      else channels[i].glissando -= channels[i].glissandoRate;
    }
    
    //burst update
    if(burst){
      if(channels[i].burst && arpegioTimer > 0) channels[i].instrument = NOISE;
      else if(channels[i].burst){
        channels[i].instrument = globalInstrument;
        channels[i].burst = false;
      }
    }
    
    //vibrato
    channels[i].vibratoPhase += vibratoTics;
    unsigned int vibrato = (vibratoAmp * (channels[i].vibratoPhase >> 8)) >> 8;
    vibrato *= 2;
    if(vibrato >= vibratoAmp)  vibrato = vibratoAmp*2-vibrato;
    if(vibratoTics <= 0) vibrato = vibratoAmp;
    
    
    //update volume, instrument and frequency
 
    if(keys[channels[i].note].pressed == true){
      oscillators[i].volume = 64-vibratoAmp+vibrato;  //sound always has a vibrato
      //oscillators[i].volume = 64;
      if(oscillators[i].waveform != channels[i].instrument ) oscillators[i].waveform = channels[i].instrument;     
      if(keys[channels[i].note].update == true){  //as the frequency is a more sensitive value to update, we only do it when the update flag is on
        setFreq(i, midi2Freq(channels[i].note)-channels[i].glissando); 
        keys[channels[i].note].update = false;      
      }       
    }
    else if(keys[channels[i].note].pressed == false){
      if(releaseRate == 0){
        oscillators[i].volume = 0;
      }
      else{
        if(oscillators[i].volume >= releaseRate) oscillators[i].volume -= releaseRate;
        else oscillators[i].volume = 0;
      }
    }

  }
  
}

ISR(TIMER1_OVF_vect)  {
  unsigned int ticks;
  byte phaseLow;
  byte volume;
  byte waveform;
  byte duty; 
  byte loopVar;
  byte waveVal;
  
  OCR1A = outputA;
  
  __asm__ volatile (
  "ldi %A[outputA], 0"  "\n\t"
  "ldi %B[outputA], 0"  "\n\t"  //these 2 lines bring outputA to 0
  "ldi %[loopVar], 8"  "\n\t"  //this line start the counter at 8 for the loop
  "LoopBegin:"  "\n\t"
  //Loop 8 times
  
  //loads the 2 bytes of the ticks var of the oscillator
  "ld %A[ticks], %a[oscBaseAddress]+"  "\n\t"
  "ld %B[ticks], %a[oscBaseAddress]+"  "\n\t"
  //we load the low byte of the phase var
  "ld %[phaseLow], %a[oscBaseAddress]"  "\n\t"
  //the high byte of the phase var is the value of the wave (sawtooth)
  "ldd %[waveVal], %a[oscBaseAddress]+1"  "\n\t"
  
  //add the ticks to the phase variable (and the wave value)
  "ldi r21, 0"  "\n\t"
  "add %[phaseLow], %A[ticks]"  "\n\t" 
  "adc %[waveVal], %B[ticks]"  "\n\t"
  "rol r21"  "\n\t"
  
  //end noise shift
  
  //we update the phase var of the oscillator with the new calculated ones
  "st %a[oscBaseAddress]+,%[phaseLow]"  "\n\t" 
  "st %a[oscBaseAddress]+,%[waveVal]"  "\n\t" 
  
  //we load the waveform of the oscillator (unused for now)
  //and also the duty value
  "ld %[waveform],%a[oscBaseAddress]+"  "\n\t" 
  "ld %[dutyVol],%a[oscBaseAddress]+"  "\n\t"  
  
  //"tst %[dutyVol]"  "\n\t"
  //"breq BreakLoop"  "\n\t"    

  //"tst %[waveform]"  "\n\t" //we check what waveform we have setup
  //"brne NotSquare"  "\n\t"
  
  "cpi %[waveform], 1"  "\n\t"  //Esto es una especie de switch
  "breq Saw"  "\n\t"
  "cpi %[waveform], 2"  "\n\t"
  "breq Triangle"  "\n\t"
  "cpi %[waveform], 3"  "\n\t"
  "breq Noise"  "\n\t"
  
  //from here the phaseLow variable will be the wave level
  
  //square branch

  "cp %[waveVal], %[dutyVol]"  "\n\t" //we compare the wave value to the duty volume
  "brlo SquareOff"  "\n\t"  
  "ser %[phaseLow]"  "\n\t"    //if waveVal > duty then the wave is high
  "rjmp VolumeMult"  "\n\t"
  "SquareOff:"  "\n\t"
  "clr %[phaseLow]"  "\n\t"    //if waveVal < duty then the wave is low
  "rjmp VolumeMult"  "\n\t"
  
  "Saw:"  "\n\t"  //saw branch

  "mov %[phaseLow],%[waveVal]"  "\n\t"
  "rjmp VolumeMult"  "\n\t"
  
  "Triangle:"  "\n\t"  //triangle branch

  "lsl %[waveVal]"  "\n\t" //left shift phase
  "brcc Ascending"  "\n\t" 
  "com %[waveVal]"  "\n\t" //if there is a carry do a xor
  "Ascending:"  "\n\t" //if ascending do nothing else
  
  //MASK triangle into 4 bits
  "cbr %[waveVal], 15"  "\n\t" //cbr clears the bits in register, its like 'register AND NOT mask' so for example cbr 170, 15 = 10101010 AND (11111111 - 00001111) = 10100000
  //that means that from 0 to 15 the wave value is 0,  from 16 to 31 the value is 16, from 32 to 47 the value is 31 etc...
  //~~
  
  "mov %[phaseLow],%[waveVal]"  "\n\t"  //ascending part if triangle
  "rjmp VolumeMult"  "\n\t"
 
  "Noise:"  "\n\t"  //This is the branch if its not a triangle
  
  //noise shift
  "tst r21"  "\n\t" //we check what waveform we have setup
  "brne EndShift"  "\n\t" 
  "ldi r20, 2"  "\n\t"
  "lsl r8"  "\n\t"
  "rol r9"  "\n\t"
  "brvc EndShift"  "\n\t"
  "eor r8, r20"  "\n\t"
  "EndShift:"  "\n\t"
  "mov %[phaseLow], r9"  "\n\t"  
  "rjmp VolumeMult"  "\n\t"
  
 
  //"Another:"  "\n\t"  //new branch
    
  "VolumeMult:"  "\n\t"  //volume 0 to 255
  //we load the volume var of the oscillator into dutyVol
  "ld %[dutyVol],%a[oscBaseAddress]+"  "\n\t"
  "mul %[phaseLow],%[dutyVol]"  "\n\t"
  "clr r0"  "\n\t"
  
  
  //CENTRAMOS SEÑAL  añade 32 operaciones extra
  "ldi r20, 64"  "\n\t" //usamos 64 porque es 512 / 8 ya que el volumen total es 512 y pueden haber 8 canales cada uno con volumen maximo de 64
  "sub r20, %[dutyVol]"  "\n\t" //substraemos a 64 el volumen actual
  "lsr r20"  "\n\t" //desplazamos el registro a la derecha para dividirlo por 2.  ej: si el volumen es 64  r20 = (64-64)/2 = 0     si el volumen es 32 r20 = (64-32)/2 = 16;
  "add r1,r20"  "\n\t"  //le sumamos al valor de la onda r20 para centrarla
  //
  
  
  "add %A[outputA],r1"  "\n\t"
  "adc %B[outputA],r0"  "\n\t"  //outputA puede ir de 0 a 511 por lo que el volumen maximo es 512/8 = 64
  
  //End of loop
  //"BreakLoop:"
  "dec %[loopVar]"  "\n\t"
  "brne LoopBegin"  "\n\t" //if loopVar > 0 go to LoopBegin
  :
  [ticks] "=&d" (ticks),
  [phaseLow] "=&d" (phaseLow),
  [waveform] "=&d" (waveform),
  [dutyVol] "=&d" (duty),
  [loopVar] "=&d" (loopVar),
  [outputA] "=&d" (outputA),
  [waveVal] "=&d" (waveVal)  
  :
  [oscBaseAddress] "y" (&oscillators[0].ticks)
  :
  "r1", "r21", "r20"
  );
  
}

void setFreq(int voice, int freq)  {
  int ticks = freq*65535UL/31250;
  oscillators[voice].ticks = ticks;
}

int midi2Freq(byte note)  {
  byte noteIndex = (note) % 12;  //figure which value in note array corresponds to
  //  recieved MIDI note
  byte noteOctave = (note) / 12; //figure out what octave the note is
  int Hz = notes[noteIndex] / (0x200 >> noteOctave);
  return Hz;
}
