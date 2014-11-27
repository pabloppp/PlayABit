
#include <SPI.h>
#include <MIDI.h>

MIDI_CREATE_DEFAULT_INSTANCE();

const int notes[] = {
  2093, 2217, 2349,    // other octaves by dividing by power of 2
  2489, 2637, 2794, 2960,   //C7 - B7
  3136, 3322, 3520, 3729, 3951
};

#define SQUARE 0
#define SAW 1
#define TRI 2
#define NOISE 3

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
  volatile byte vibratoPhase; 
  volatile byte arpegioTimer; //done
};

struct channelStruct channels[8];

struct keyStruct {
  boolean pressed;
  byte channel;
  boolean update;
};

struct keyStruct keys[128];

byte lastChannel = 0;

// volatile variables used in ISR
volatile int outputA = 0;


//not volatile variables
byte ledTimer, arpegioTimer;
byte arpegioDelay = 4;  //if another key is pressed within 0.2s start arpedio mode
boolean ledOn = false;
boolean arpegioMode = false;
boolean glissandoMode = true;
byte releaseRate = 0;
boolean started = false;


byte globalWaveform = SQUARE;
byte globalDutyCycle = 31; //50%: 127   25%: 63  12.5%: 31

void setup(){    
  setupTimer();
  
  pinMode(13, OUTPUT);  //LED 13
  ledTimer = arpegioTimer = 0;
  
  //start melody
  oscillators[0].dutyCycle = 127;
  oscillators[0].volume = 20;
  oscillators[0].waveform = 0;
  oscillators[1].dutyCycle = 127;
  oscillators[1].volume = 20;
  oscillators[1].waveform = 2;
  oscillators[2].dutyCycle = 127;
  oscillators[2].waveform = 3;
  oscillators[0].ticks = (notes[0] / (0x200 >> 6))*65535UL/31250;
  oscillators[1].ticks = (notes[7] / (0x200 >> 4))*65535UL/31250;
  delay(100);
  oscillators[0].ticks = (notes[4] / (0x200 >> 6))*65535UL/31250;
  delay(100);
  oscillators[0].ticks = (notes[7] / (0x200 >> 6))*65535UL/31250;
  oscillators[1].ticks = (notes[0] / (0x200 >> 5))*65535UL/31250;
  delay(100);
  oscillators[0].ticks = (notes[0] / (0x200 >> 7))*65535UL/31250;
  oscillators[1].volume = 0; 
  delay(300);
  oscillators[2].volume = 50;
  oscillators[0].volume = 0;
  setFreq(2, 15000);
  delay(20);
  oscillators[2].volume = 0;
  //~~~~

  MIDI.begin(1);
  
  for(int i=0;i<8;i++){
    channels[i].note = 0;
    channels[i].glissandoRate = 1;
    channels[i].glissando = 0;
    channels[i].vibratoPhase = 0;
    
    oscillators[i].dutyCycle = globalDutyCycle;  
    oscillators[i].volume = 0;
    oscillators[i].waveform = globalWaveform;
    oscillators[i].waveform = globalWaveform;

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
      byte data1 = MIDI.getData1();
      byte data2 = MIDI.getData2();
      switch(MIDI.getType()){
        case midi::NoteOn:
          for(int i=0;i<8;i++){
            if(glissandoMode && arpegioTimer > 0 && data1-1 == channels[lastChannel].note){
              byte g = midi2Freq(data1) - midi2Freq(channels[lastChannel].note);
              channels[lastChannel].glissando = g;
              channels[lastChannel].note = data1;
              keys[data1].channel = lastChannel;
              keys[data1].pressed = true;
              keys[data1-1].pressed = false;
              keys[data1].update = true;
              break;  
            }
            else if(arpegioMode && arpegioTimer > 0){
              keys[data1].channel = lastChannel;
              keys[data1].pressed = true;
              break;
            }
            if(keys[channels[i].note].pressed == false){
              channels[i].note = data1;
              lastChannel = i;
              keys[data1].channel = lastChannel;
              keys[data1].pressed = true;
              keys[data1].update = true;
              break;
            }
          }
          arpegioTimer = arpegioDelay;
          break;
        
        case midi::NoteOff:
            for(int i=0;i<8;i++){
                keys[data1].pressed = false;
                keys[data1].update = true;
                break;
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
          //if(newNote > 127) newNote -= 127;
          if(keys[newNote].pressed == true && keys[newNote].channel == i && channels[i].note != newNote){
            channels[i].note = newNote;
            keys[newNote].update = true;
            //setFreq(i, midi2Freq(channels[i].note));  
            digitalWrite(13, HIGH);
            break;
          }
          newNote -= 1;
        }
        
      }
      
    }
    //~~
    
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
  /*
  if(ledTimer > 25 && !ledOn){
    digitalWrite(13, HIGH);
    ledOn = true;
    //oscillators[2].volume = 10;
  }
  
  if(ledTimer > 27){
    //oscillators[2].volume = 0;
  }
  
  if(ledTimer > 50 && ledOn){
    digitalWrite(13, LOW);
    ledOn = false;
    ledTimer = 0;
  }

  ledTimer++;
  */
  if(arpegioTimer > 0){
    arpegioTimer--;
  }
  
  for(int i=0;i<8;i++){
    
    if(arpegioMode && channels[i].arpegioTimer > 0){
      channels[i].arpegioTimer--;
    }
    
    if(glissandoMode && channels[i].glissando > 0){       
       //setFreq(i, midi2Freq(channels[i].note)-channels[i].glissando); 
       keys[channels[i].note].update = true;
       channels[i].glissando -= channels[i].glissandoRate;
    }
    
    if(keys[channels[i].note].pressed == true && keys[channels[i].note].update == true){
      keys[channels[i].note].update = false;
      oscillators[i].volume = 64;
      setFreq(i, midi2Freq(channels[i].note)-channels[i].glissando);  
    }
    else if(keys[channels[i].note].pressed == false && keys[channels[i].note].update == true){
      if(releaseRate == 0){
        keys[channels[i].note].update = false;
        oscillators[i].volume = 0;
      }
      else{
        if(oscillators[i].volume > 0) oscillators[i].volume -= releaseRate;
        else if(oscillators[i].volume <= 0){
          oscillators[i].volume = 0;
          keys[channels[i].note].update = false;
        }
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
  
  "tst %[dutyVol]"  "\n\t"
  "breq BreakLoop"  "\n\t"    

  //"tst %[waveform]"  "\n\t" //we check what waveform we have setup
  //"brne NotSquare"  "\n\t"
  
  "cpi %[waveform], 1"  "\n\t"  //Esto es una especie de switch
  "breq Saw"  "\n\t"
  "cpi %[waveform], 2"  "\n\t"
  "breq Triangle"  "\n\t"
  "cpi %[waveform], 3"  "\n\t"
  "breq Noise"  "\n\t"
  
  //Branch for square wave
  //from here the phaseLow variable will be the wave level
  "cp %[waveVal], %[dutyVol]"  "\n\t" //we compare the wave value to the duty volume
  "brlo SquareOff"  "\n\t"  
  "ser %[phaseLow]"  "\n\t"    //if waveVal > duty then the wave is high
  "rjmp VolumeMult"  "\n\t"
  "SquareOff:"  "\n\t"
  "clr %[phaseLow]"  "\n\t"    //if waveVal < duty then the wave is low
  "rjmp VolumeMult"  "\n\t"
  
  "Saw:"  "\n\t"  //This is the branch if its not a square
  //"dec %[waveform]"  "\n\t"
  //"tst %[waveform]"  "\n\t" //we check what waveform we have setup
  //"brne NotSaw"  "\n\t"
  //Branch for saw wave
  "mov %[phaseLow],%[waveVal]"  "\n\t"
  "rjmp VolumeMult"  "\n\t"
  
  "Triangle:"  "\n\t"  //This is the branch if its not a saw
  ///"dec %[waveform]"  "\n\t"
  ///"tst %[waveform]"  "\n\t" //we check what waveform we have setup
  ///"brne NotTriangle"  "\n\t"
  //Branch for triangle wave
  "lsl %[waveVal]"  "\n\t" //left shift phase
  "brcc Ascending"  "\n\t" 
  "com %[waveVal]"  "\n\t" //if there is a carry do a xor
  
  "Ascending:"  "\n\t"
  
  "mov %[phaseLow],%[waveVal]"  "\n\t"  //ascending part if triangle
  "rjmp VolumeMult"  "\n\t"
 
  "Noise:"  "\n\t"  //This is the branch if its not a triangle
  ///"dec %[waveform]"  "\n\t"
  ///"tst %[waveform]"  "\n\t" //we check what waveform we have setup
  ///"brne NotNoise"  "\n\t"
  
  //noise shift
  "tst r21"  "\n\t" //we check what waveform we have setup
  "brne NoNoiseShift"  "\n\t" 
  "ldi r20, 2"  "\n\t"
  "lsl r8"  "\n\t"
  "rol r9"  "\n\t"
  "brvc skipShift"  "\n\t"
  "eor r8, r20"  "\n\t"
  "skipShift:"  "\n\t"
  "NoNoiseShift:"  "\n\t"
  
  "mov %[phaseLow],r9"  "\n\t"  
  "rjmp VolumeMult"  "\n\t"
  
 
  //"NotNoise:"  "\n\t"  //This is the branch if its not a noise
    
  "VolumeMult:"  "\n\t"  //volume 0 to 255
  //we load the volume var of the oscillator into dutyVol
  "ld %[dutyVol],%a[oscBaseAddress]+"  "\n\t"
  "mul %[phaseLow],%[dutyVol]"  "\n\t"
  "clr r0"  "\n\t"
  "add %A[outputA],r1"  "\n\t"
  "adc %B[outputA],r0"  "\n\t"
  
  //End of loop
  "BreakLoop:"
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
