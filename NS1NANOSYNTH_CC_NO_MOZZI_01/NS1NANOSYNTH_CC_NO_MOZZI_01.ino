//NS1nanosynth firmware NS1NANOSYNTH_CC_NO_MOZZI_01
//
//This is the basic software with CC# mapped to the digipot.
//In this version MOZZI is not installed, there are problems with I2C compatibility.
//To supply anyway a simple second oscillator with MIDI we use the tone output on the D9 pin.
//A solution could be either to use twi_non_block functions OR implement a software I2C (more CPU-time consuming)
//MIDI is OK on channel 1, pitch bend is managed (+/- 2 semitones) ,modwheel is on DAC1.
//CC numbers are from 30 to 33 mapped to digipot A to D. Mapping is a simple multiplication by two.
//remember that DIGIPOTS are 'floating' and they should be connected as the user wants!
//to setup the main timebase that checks USB midi events, we use the Timer1 functions.
//See Fritzing sketches for reference

//modification also includes: 
//                          CODE CLEANING
//
//
//insert instruction on arcore and lins installation here:
//........................................................


#include "Wire.h"       //i2c lib to drive the quad digipot chip
#include <TimerOne.h>   //Timer lib to generate interrupt callback (used for MIDI USB checking)

#include <SPI.h>         // Remember this line!
#include <DAC_MCP49xx.h> // DAC libs (remember we use an external ref voltage of 2.5V)

DAC_MCP49xx dac(DAC_MCP49xx::MCP4922, 4, -1);   //NS1nanosynth has DAC SS on pin D4

#define MIN_NOTE 36
#define MAX_NOTE MIN_NOTE+61
#define GATE_PIN 5 // default GATE pin on NS1nanosynth.
#define TRIGGER_PIN 6
#define NOTES_BUFFER 127

// Define the CC numbers used to control the digipots
// dturner - I have modified these to match rotary controls 1-4 on a Novation LaunchKey 49
#define MIDI_CC_NUMBER_FOR_DIGIPOT_A 21
#define MIDI_CC_NUMBER_FOR_DIGIPOT_B 22
#define MIDI_CC_NUMBER_FOR_DIGIPOT_C 23
#define MIDI_CC_NUMBER_FOR_DIGIPOT_D 24

const byte NOTEON = 0x09;
const byte NOTEOFF = 0x08;
const byte CC = 0x0B;
const byte PB = 0x0E;

const int kTriggerDurationMillis = 1; 

//////////////////////////////////////////////////////////////
// start of variables that you are likely to want to change
//////////////////////////////////////////////////////////////
byte MIDI_CHANNEL = 0; // Initial MIDI channel (0=1, 1=2, etc...), can be adjusted with notes 12-23
//////////////////////////////////////////////////////////////
// end of variables that you are likely to want to change
//////////////////////////////////////////////////////////////

// Starting here are things that probably shouldn't be adjusted unless you're prepared to fix/enhance the code.
unsigned short notePointer = 0;
int notes[NOTES_BUFFER];
int noteNeeded=0;
float currentNote=0;
byte analogVal = 0;
float glide=0;
int mod=0;
float currentMod=0;
int bend=0;
bool isTriggerOn = false;
int triggerOffTime = 0;


int DacVal[] = {0, 68, 137, 205, 273, 341, 410, 478, 546, 614, 683, 751, 819, 887, 956, 1024, 1092, 1160, 1229, 1297, 1365, 
1433, 1502, 1570, 1638, 1706, 1775, 1843, 1911, 1979, 2048, 2116, 2184, 2252, 2321, 2389, 2457, 2525, 2594, 2662, 2730, 2798,
2867, 2935, 3003, 3071, 3140, 3208, 3276, 3344, 3413, 3481, 3549, 3617, 3686, 3754, 3822, 3890, 3959, 4027, 4095};

byte addresses[4] = { 0x00, 0x10, 0x60, 0x70 }; //digipot address
byte digipot_addr= 0x2C;  //i2c bus digipot IC addr
byte valorepot=0; //only for debug routine... delete.
byte ccpot0_ready=0;
byte ccpot1_ready=0;
byte ccpot2_ready=0;
byte ccpot3_ready=0;
byte pot0=0;
byte pot1=0;
byte pot2=0;
byte pot3=0;

unsigned short DacOutA=0;
unsigned short DacOutB=0;

void setup(){
  pinMode( GATE_PIN, OUTPUT ); // set GATE pin to output mode
  analogWrite( GATE_PIN, 0);  //GATE down
  pinMode( TRIGGER_PIN, OUTPUT); // set TRIGGER pin to output mode
  analogWrite( TRIGGER_PIN, 0); // TRIGGER down
  dac.setGain(2);
  Wire.begin();

  // Set a timer to call updateNS1() every X us
  Timer1.initialize(1000);          
  Timer1.attachInterrupt(updateNS1); 
}

void i2c_send(byte addr, byte a, byte b)      //wrapper for I2C routines
{
    Wire.beginTransmission(addr);
    Wire.write(a);
    Wire.write(b);
    Wire.endTransmission();
}

void DigipotWrite(byte pot,byte val)        //write a value on one of the four digipots in the IC
{
  i2c_send( digipot_addr, 0x40, 0xff );
  i2c_send( digipot_addr, 0xA0, 0xff );
  i2c_send( digipot_addr, addresses[pot], val);  
}

void updateNS1(){
  while(MIDIUSB.available() > 0) { 
    // Repeat while notes are available to read.
    MIDIEvent e;
    e = MIDIUSB.read();
    if(e.type == NOTEON) {
      if(e.m1 == (0x90 + MIDI_CHANNEL)){
        if(e.m2 >= MIN_NOTE && e.m2 <= MAX_NOTE){
          if(e.m3==0)         //Note in the right range, if velocity=0, remove note
            removeNote(e.m2);
          else                //Note in right range and velocity>0, add note
            addNote(e.m2);              
        } else if (e.m2 < MIN_NOTE) {
          //out of lower range hook      
        } else if (e.m2 > MAX_NOTE) {
          //out of upper range hook
        }
      }
    }
    
    if(e.type == NOTEOFF) {
      if(e.m1 == 0x80 + MIDI_CHANNEL){
        removeNote(e.m2);
      }
    }
    
    // set modulation wheel
    if (e.type == CC && e.m2 == 1)
    {
      if (e.m3 <= 3)
      {
        // set mod to zero
       mod=0;
       dac.outputB(0);
      } 
      else 
      {
        mod=e.m3;
        DacOutB=mod*32;
        dac.outputB(DacOutB);
      }
    }

    //set digipots A to D with CC from 30 to 33
    if (e.type == CC && e.m2 == MIDI_CC_NUMBER_FOR_DIGIPOT_A){
      ccpot0_ready=1;
      pot0=e.m3<<1;
    }
    if (e.type == CC && e.m2 == MIDI_CC_NUMBER_FOR_DIGIPOT_B){
      ccpot1_ready=1;
      pot1=e.m3<<1;
    }
    if (e.type == CC && e.m2 == MIDI_CC_NUMBER_FOR_DIGIPOT_C){
      ccpot2_ready=1;
      pot2=e.m3<<1;
    }
    if (e.type == CC && e.m2 == MIDI_CC_NUMBER_FOR_DIGIPOT_D){
      ccpot3_ready=1;
      pot3=e.m3<<1;
    }

    // set pitch bend
    if (e.type == PB){
     if(e.m1 == (0xE0 + MIDI_CHANNEL)){
        // map bend somewhere between -127 and 127, depending on pitch wheel
        // allow for a slight amount of slack in the middle (63-65)
        // with the following mapping pitch bend is +/- two semitones
        if (e.m3 > 65){
          bend=map(e.m3, 64, 127, 0, 136);
        } else if (e.m3 < 63){
          bend=map(e.m3, 0, 64, -136, 0);
        } else {
          bend=0;
        }
        
        if (currentNote>0){
          playNote (currentNote, 0);
        }
      }
    }
      
    MIDIUSB.flush();
  } // end while
   
  if (noteNeeded>0){
    // on our way to another note
    if (currentNote==0){
      // play the note, no glide needed
      playNote (noteNeeded, 0);
      
      // set last note and current note, clear out noteNeeded becase we are there
      currentNote=noteNeeded;
      noteNeeded=0;
    } else {
      if (glide>0){
        // glide is needed on our way to the note
        if (noteNeeded>int(currentNote)) {
          currentNote=currentNote+glide;
          if (int(currentNote)>noteNeeded) currentNote=noteNeeded;     
        } else if (noteNeeded<int(currentNote)) {
          currentNote=currentNote-glide;
          if (int(currentNote)<noteNeeded) currentNote=noteNeeded;
        } else {
          currentNote=noteNeeded;
        }
      } else {
        currentNote=noteNeeded;
      }
      playNote (int(currentNote), 0);
      if (int(currentNote)==noteNeeded){
        noteNeeded=0;
      }
    }
  } else {
    if (currentNote>0){
    }
  }

  int currentTime = millis();

  if (isTriggerOn && triggerOffTime < currentTime){
    isTriggerOn = false;
    analogWrite(TRIGGER_PIN, 0);
  }
} // end updateNS1

void loop(){
  //it is necessary to move the i2c routines out of the callback. probably due to some interrupt handling!
  if(ccpot0_ready){
    DigipotWrite(0,pot0);
    ccpot0_ready=0;
  }
  if(ccpot1_ready){
    DigipotWrite(1,pot1);
    ccpot1_ready=0;
  } 
  if(ccpot2_ready){
    DigipotWrite(2,pot2);
    ccpot2_ready=0;
  }
  
  if(ccpot3_ready){
    DigipotWrite(3,pot3);
    ccpot3_ready=0;
  }
} // end loop

void playNote(byte noteVal, float myMod) {
  analogVal = map(noteVal, MIN_NOTE, MAX_NOTE, 0, 2550)/10;  //  analogVal = map(noteVal, MIN_NOTE, MAX_NOTE, 0, 2550+oscAdjust)/10;
  if (analogVal > 255) {
    analogVal=255; 
  }
  
  // see if this note needs pitch bend
  if (bend != 0) { analogVal=analogVal+bend; }

  int DacOutA=DacVal[noteVal-MIN_NOTE];
  if (bend != 0) { DacOutA=DacOutA+bend; }
  dac.outputA(DacOutA);
  analogWrite(GATE_PIN, 255); //GATE ON
  
  analogWrite(TRIGGER_PIN, 255); // TRIGGER ON
  isTriggerOn = true;
  triggerOffTime = millis() + kTriggerDurationMillis;
} // end playNote

void addNote(byte note){
  boolean found=false;
  // a note was just played
  
  // see if it was already being played
  if (notePointer>0){
    for (int i=notePointer; i>0; i--){
      if (notes[i]==note){
        // this note is already being played
        found=true;
        
        // step forward through the remaining notes, shifting each backward one
        for (int j=i; j<notePointer; j++){
          notes[j]=notes[j+1];
        }
        
        // set the last note in the buffer to this note
        notes[notePointer]=note;
        
        // done adding note
        break;
      }
    }
  }
  
  if (found==false){
    // the note wasn't already being played, add it to the buffer
    notePointer=(notePointer+1) % NOTES_BUFFER;
    notes[notePointer]=note; 
  }
  
  noteNeeded=note;
} // end addNote

void removeNote(byte note){
  boolean complete=false;
  
  // handle most likely scenario
  if (notePointer==1 && notes[1]==note){
    // only one note played, and it was this note
    notePointer=0;
    currentNote=0;
    
    // turn light off
    analogWrite(GATE_PIN, 0);
 
  } else {
    // a note was just released, but it was one of many
    for (int i=notePointer; i>0; i--){
      if (notes[i]==note){
        // this is the note that was being played, was it the last note?
        if (i==notePointer){
          // this was the last note that was being played, remove it from the buffer
          notes[i]=0;
          notePointer=notePointer-1;
          
          // see if there is another note still being held
          if (i>1){
            // there are other that are still being held, sound the most recently played one
            addNote(notes[i-1]);
            complete=true;
          }
        } 
        else{
          // this was not the last note that was being played, just remove it from the buffer and shift all other notes
          for (int j=i; j<notePointer; j++){
            notes[j]=notes[j+1];
          }
          
          // set the last note in the buffer to this note
          notes[notePointer]=note;
          notePointer=notePointer-1;
          complete=true;
        }
        
        if (complete==false){
          // we need to stop all sound
          //analogWrite(NOTE_PIN1, 0);
          
          // make sure notePointer is cleared back to zero
          notePointer=0;
          currentNote=0;
          noteNeeded=0;
          break;
        }
        
        // finished processing the release of the note that was released, just quit
        break;
        //F<3
      }
    }
  }
} // end removeNote
