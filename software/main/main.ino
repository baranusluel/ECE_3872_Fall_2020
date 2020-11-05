#include <Adafruit_VL53L0X.h>
#include <Servo.h>

////////////////////////////////
// Constants
////////////////////////////////

#define BUZZER_PIN A1
#define LED_R_PIN 0
#define LED_G_PIN 1
#define LED_B_PIN 2
#define ROTARY_SWITCH_PIN A0
#define RESET_BTN_PIN 12
#define DC_STEP_PIN 6
#define DC_DIR_PIN 5
#define SERVO_PIN 10

#define RECORDING_MAX_LEN 40
const float noteFreqs[] =
   {110.0f, 123.471f, 130.813f, 146.832f, 164.814f, 174.614f, 195.998f, 207.652f};

////////////////////////////////
// Global variables
////////////////////////////////

enum State {
   STOP, // IDLE
   RECORD,
   PLAY_LIVE,
   PLAY_RECORD
} state = STOP; // State machine
State oldMode = State::STOP;

int recording[RECORDING_MAX_LEN]; // Memory for recording up to 40 half-second notes
int recordingLen = 0; // Length of the recording. Used for appending to end during recording
int playbackIndx = 0; // Current index in recording array for playback

int resetButtonCounter = 0; // Counts how long reset button is pressed

int oldNote = -1; // The previous note that was played, to check if new note is different

Adafruit_VL53L0X lox = Adafruit_VL53L0X();
Servo servo;

////////////////////////////////
// Helper function declarations
////////////////////////////////

/// State handlers:

// On recording reset (button pressed 3 sec)
void reset_recording();
// Changes to specified state and runs one-time setup for state
void enter_idle();
void enter_record();
void enter_play_live();
void enter_play_record();
// On every loop while in a state
void loop_idle();
void loop_record();
void loop_play_live();
void loop_play_record();

/// IO Modules:

// Returns the note to be played according to the input (distance) sensor.
// -1 = none, 0-7 = A2,B2,C3,D3,E3,F3,G3,G#3
int read_input_note();
// Plays the specified note on the speaker for a 0.5s duration.
void play_note(int note);
// Moves motors in a manner corresponding to the played note.
void move_for_note(int note);
// Sets color of RGB LED
void set_rgb_led(int red, int green, int blue);
// Get mode from rotary switch
State get_mode();
// Sends given number of step pulses to the motor driver
void pulse_motor(int steps);

////////////////////////////////
// Entry Points
////////////////////////////////

void setup() {
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_R_PIN, OUTPUT);
  pinMode(LED_G_PIN, OUTPUT);
  pinMode(LED_B_PIN, OUTPUT);
  pinMode(RESET_BTN_PIN, INPUT);
  pinMode(DC_STEP_PIN, OUTPUT);
  pinMode(DC_DIR_PIN, OUTPUT);
  
  servo.attach(SERVO_PIN);
  lox.begin();
  
  enter_idle();
  oldMode = get_mode();

  // Put stepper driver translator in correct state (zero current) by
  // sending 4 pulses (found empirically), and then send 8 to move it up
  // for starting position, 8 after delay to stop motion.
  pulse_motor(4 + 8);
  delay(750);
  pulse_motor(8);
}

// Main loop method manages state changes and calls current state handlers.
// During normal execution, runs twice a second.
void loop() {
   State currMode = get_mode();
   // Change state only if mode switch changes
   if (currMode != oldMode) {
    oldMode = currMode;
    if (currMode == State::STOP) enter_idle();
    else if (currMode == State::RECORD) enter_record();
    else if (currMode == State::PLAY_LIVE) enter_play_live();
    else if (currMode == State::PLAY_RECORD) enter_play_record();
   }
      
   // Reset button overrides selected state and enters idle
   if (digitalRead(RESET_BTN_PIN)) {
      // If reset button was pressed for 3 seconds (counted every 0.5s)
      if (resetButtonCounter >= 3) {
         reset_recording();
         resetButtonCounter = 0;
      } else {
         resetButtonCounter++;
      }
      enter_idle();
   } else {
      resetButtonCounter = 0;
   }
   
   // Run state loop handler for current state
   switch (state) {
      case State::STOP:
         loop_idle();
        break;
      case State::RECORD:
         loop_record();
         break;
      case State::PLAY_LIVE:
         loop_play_live();
         break;
      case State::PLAY_RECORD:
         loop_play_record();
         break;
   }
}

////////////////////////////////
// Helper function definitions
////////////////////////////////

void reset_recording() {
   // 'reset' recording by zeroing the length. Array will still
   // have old values but they will be unused / overwritten.
   recordingLen = 0;

   // Flashes LED three times to indicate successful reset
   for (int i = 0; i < 3; i++) {
      set_rgb_led(1, 1, 1);
      delay(150);
      set_rgb_led(0, 0, 0);
      delay(150);
   }
}

void enter_idle() {
   state = State::STOP;

   // Blue LED when idling
   set_rgb_led(0, 0, 1);
   // Turn off motor
   move_for_note(-1);
   // Turn off speaker
   play_note(-1);
}

void enter_record() {
   state = State::RECORD;

   // Red LED when recording
    for (int i = 0; i < 3; i++) {
      set_rgb_led(1, 0, 0);
      delay(250);
      set_rgb_led(0, 0, 0);
      delay(250);
   }
   set_rgb_led(1, 0, 0);
   // Turn off motor
   move_for_note(-1);
}

void enter_play_live() {
   state = State::PLAY_LIVE;

   // Cyan LED when playing live
   set_rgb_led(0, 1, 1);

   oldNote = -1;
}

void enter_play_record() {
   state = State::PLAY_RECORD;

   // Green LED when playing recording
   set_rgb_led(0, 1, 0);
   // Reset playback index for recording array
   playbackIndx = 0;

   oldNote = -1;
}

void loop_idle() {
  delay(1000);
}

void loop_record() {
   // Read input sensor and play note
   int note = read_input_note();
   play_note(note);
   // Save note into recording, appending to the end of the array
   // if we haven't exceeded maximum length
   if (recordingLen < RECORDING_MAX_LEN) {
      recording[recordingLen] = note;
      recordingLen++;
   } else {
      // If exceeded maximum duration, indicate with dark LED
      set_rgb_led(0, 0, 0);
   }
   delay(1000);
}

void loop_play_live() {
   // Read input sensor, play note and move motors to the note
   int note = read_input_note();
   play_note(note);
   if (note != oldNote) {
     // Delay included in movement timing
     move_for_note(note);
     oldNote = note;
   } else {
     delay(1000);
   }
}

void loop_play_record() {
   // Play note from recording array, if length is non-zero
   if (recordingLen > 0) {
      playbackIndx = playbackIndx % recordingLen; // playback loops
      int note = recording[playbackIndx];
      playbackIndx++;
      play_note(note);

      if (note != oldNote) {
        // Move motor to note.
        // Delay included in movement timing
        move_for_note(note);
        oldNote = note;
      } else {
        delay(1000);
      }
   } else {
      play_note(-1);
      delay(1000);
   }
}

void play_note(int note) {
   if (note < 0 || note > 7)
      noTone(BUZZER_PIN);
   else
      tone(BUZZER_PIN, noteFreqs[note], 1000);
}

void move_for_note(int note) {
  if (note < 0 || note > 7) {
    servo.write(180);
  } else {
    // Move servo horizontally
    servo.write(180 - (note + 1) * 22.5); // divide range of motion equally into intervals of 180/8=22.5
    delay(300);
    // Spining DC motor up and down to hit drum
    pulse_motor(8); // stopped -> down
    // Delay to allow time for vertical motion
    delay(50);
    pulse_motor(16); // down -> stopped -> up
    delay(650);
    pulse_motor(8); // up -> stopped
  }
}

void pulse_motor(int steps) {
  for (int i = 0; i < steps; i++) {
      digitalWrite(DC_STEP_PIN, HIGH);
      delayMicroseconds(5);
      digitalWrite(DC_STEP_PIN, LOW);
      delayMicroseconds(5);
    }
}

void set_rgb_led(int red, int green, int blue) {
  digitalWrite(LED_R_PIN, red);
  digitalWrite(LED_G_PIN, green);
  digitalWrite(LED_B_PIN, blue);
}

State get_mode() {
  int sensorValue = analogRead(ROTARY_SWITCH_PIN);
  if (sensorValue < 170) {
    return State::STOP;
  } else if (sensorValue < 511){
    return State::RECORD;
  } else if (sensorValue < 852) {
    return State::PLAY_RECORD;
  } else {
    return State::PLAY_LIVE;
  }
}

int read_input_note(){
  VL53L0X_RangingMeasurementData_t measure;
  int note = 8;
  lox.rangingTest(&measure, false);
  if (measure.RangeStatus != 4) {  // phase failures have incorrect data
    if (measure.RangeMilliMeter > 50 && measure.RangeMilliMeter <= 100){
      note = 0;
     } else if (measure.RangeMilliMeter > 100 && measure.RangeMilliMeter <= 150) {
      note = 1;
     } else if (measure.RangeMilliMeter > 150 && measure.RangeMilliMeter <= 200) {
      note = 2;
     } else if (measure.RangeMilliMeter > 200 && measure.RangeMilliMeter <= 250) {
      note = 3;
     } else if (measure.RangeMilliMeter > 250 && measure.RangeMilliMeter <= 300) {
      note = 4;
     } else if (measure.RangeMilliMeter > 300 && measure.RangeMilliMeter <= 350) {
      note = 5;
     } else if (measure.RangeMilliMeter > 350 && measure.RangeMilliMeter <= 400) {
      note = 6;
     } else if (measure.RangeMilliMeter > 400 && measure.RangeMilliMeter <= 450) {
      note = 7;
     }
  }
  return note;
}

