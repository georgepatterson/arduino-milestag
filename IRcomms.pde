//The IR communication components.
//
// Hacked together by Andrew Shirley

// Code which was either 'copypasta'ed or used as inspiration at some point:
//
// http://www.arduino.cc/cgi-bin/yabb2/YaBB.pl?num=1240843250 Copyright (C) Timo Herva aka 'mettapera', 2009
// http://tthheessiiss.wordpress.com/2009/08/05/dirt-cheap-wireless/ 

// TODO: the protocol says LSB first, i.e. you can't just shift and add (you have to use (oldValue shifted appropriately AND 1)).
//      use another CTC timer to tell when to set the ir up or down (not to be confused with the carrier frequency!)

//NB. as these are quite time sensitive enableing this often breaks it :-(
//#define DEBUG_SEND 1
//#define DEBUG_RECV 1
#define DEBUG_DECODE 1


// pin numbers (9 is used for a Carrier wave and so isn't available)
byte pin_infrared = 8;
byte pin_ir_feedback = 13;
byte pin_ir_reciever = 12;
//byte pin_ir_reciever_port = PORTB;
byte pin_ir_reciever_bit = 0;

// some timings
long headerDuration = 2400;
long intervalDuration = 600;
long oneDuration = 1200;
long zeroDuration = 600;

byte timingTolerance = 100;

////////////////////////
// IR Writing variables
//byte volume_up = 0x24;//B0100100
byte volume_up = B0001100;

byte writeBuffer = 0;
byte writeBits = 0;
unsigned long writeUpTime = 0;
unsigned long writeDownTime = 0;
unsigned long writeLastChangeTime = 0;

////////////////////////
// IR reading variables
byte readBuffer = 0;
byte bitsRead = 0;

byte oldPinValue = 0;

//the micros for the point at which the IR went high
unsigned long readRiseTime = 0;

//the micros for the point at which the IR went low
unsigned long readFallTime = 0;


////////////////////////
// IR reading functions

//read the IR receiver and if applicable add to the readBuffer. This will return 1 if the transmission appears to be complete. Subsequent reads will return 0.
int signal_recieve() {
  byte pinValue = bitRead(PORTB, pin_ir_reciever_bit);
  if (!oldPinValue && pinValue) {
    //IR rising edge
    //TODO: should we check that we have been low for an appropriate amount of time?
    readRiseTime = micros();
    oldPinValue = HIGH;
#ifdef DEBUG_RECV
    Serial.print("\\ @");
    Serial.println(readRiseTime);
#endif
    //there is always more to be read if the IR is high.
    return 0;
  }
  else if (oldPinValue && !pinValue) {
    //IR falling edge
    readFallTime = micros();
    unsigned long duration = readFallTime - readRiseTime;

    if (within_tolerance(duration, headerDuration, timingTolerance)) {
      //we are within tolerance of 2400 us - a restart
      readBuffer = 0;
      bitsRead = 0;
    }
    else if (within_tolerance(duration, oneDuration, timingTolerance)) {
      //we are within tolerance of 1200 us - a one
      readBuffer = (readBuffer << 1) + 1;
      bitsRead++;
    }
    else if (within_tolerance(duration, zeroDuration, timingTolerance)) {
      //we are within tolerance of 600 us - a zero
      readBuffer = readBuffer << 1;
      bitsRead++;
    }
    else {
#ifdef DEBUG_RECV
      Serial.print("/ @");
      Serial.print(microsVal);
      Serial.print("  ");
      Serial.println(duration);
#endif
    }

    oldPinValue = LOW;
    //wait to see if there is more to be read
    return 0;
  }
  else if (oldPinValue) {
    //IR continues to be high
    //there is always more to be read if the IR is high.
    return 0;
  }
  else /*if (!oldPinValue)*/ {
    //IR continues to be low
    //if we have been low for more than interval + tolerance (twice for extra leniency) we can assume the transmission has finished and try to read it.
    if (!readFallTime) {
      //we aren't waiting for an interval, all quiet on the IR front.
      return 0;
    }
    else if (micros() - readFallTime > intervalDuration + timingTolerance * 2) {
      readFallTime = 0; //cache this result
      return 1;
    }
    else {
      //still low, waiting for the interval
      return 0;
    }
  }
}

boolean within_tolerance(unsigned long value, unsigned long target, byte tolerance) {
  long remainder = value - target;
  return remainder < tolerance && remainder > -tolerance;
}

void decode_signal() {
#ifdef DEBUG_DECODE
  Serial.print("==");
  Serial.println(readBuffer, BIN);
#endif
}

////////////////////////
// IR writing functions

void start_command(byte command) {
  if (writeUpTime || writeDownTime) {
    //already writing - this is an error
    //Serial.println("tried to start a command when we are already sending");
    return;
  }
#ifdef DEBUG_SEND
  Serial.print("sending ");
  Serial.println(command, BIN);
#endif

  writeBuffer = command;
  writeBits = 8;
  
  //write header
  ir_up();
#ifdef DEBUG_SEND
  Serial.println("  \\");
#endif
  writeDownTime = headerDuration;
}

void signal_send() {
  unsigned long elapsed = micros() - writeLastChangeTime;
  
  if (writeDownTime && writeDownTime <= elapsed) {
#ifdef DEBUG_SEND
    Serial.print("  /");
    Serial.print(elapsed);
    Serial.print(" - ");
    Serial.println(writeDownTime, DEC);
#endif
    ir_down();
    writeDownTime = 0;
    
    if (writeBits) {
      //not done yet
      writeUpTime = intervalDuration;
    }
  }
  else if (writeUpTime && writeUpTime <= elapsed) {
#ifdef DEBUG_SEND
    Serial.print("  \\");
    Serial.print(elapsed);
    Serial.print(" - ");
    Serial.println(writeUpTime, DEC);
#endif
    ir_up();
    writeUpTime = 0;
    
    if (writeBuffer & B1) {
      //write a one
      writeDownTime = oneDuration;
    }
    else {
      //write a zero
      writeDownTime = zeroDuration;
    }
    
    writeBuffer = writeBuffer >> 1;
    writeBits--;
  }
}

void ir_up() {
  digitalWrite(pin_infrared, HIGH);
  digitalWrite(pin_ir_feedback, HIGH);
  writeLastChangeTime = micros();
}

void ir_down() {
  digitalWrite(pin_infrared, LOW);
  digitalWrite(pin_ir_feedback, LOW);
  writeLastChangeTime = micros();
}

////////////////////////
// general functions

//setting things ready  
void setup() {
  //set the pins
  pinMode(pin_infrared, OUTPUT);
  pinMode(9, OUTPUT);
  pinMode(pin_ir_feedback, OUTPUT);
  pinMode(pin_ir_reciever, INPUT);
  
  // see http://www.atmel.com/dyn/resources/prod_documents/doc8161.pdf for more details (page 136 onwards)
  //set the carrier wave frequency. This only sets up pin 9.
  TCCR1A = _BV(COM1A0); // | _BV(COM1B0); for another pin (10)
  TCCR1B = _BV(WGM12) | _BV(CS10);
  
  TIMSK1 = 0; //no interupts
  TIFR1 = _BV(OCF1A) | _BV(OCF1A); //clear Output Compare Match Flags (by setting them :-P )
  unsigned long desired_freq = 40000;
  OCR1A = 10000000/desired_freq - 1; // see page 126 of datasheet for this equation
  //OCR1B = 10000000/desired_freq - 1; for another pin (10)

  ir_down();

  //debug  
  Serial.begin(9600); 
  Serial.println("jobbie - debug");
}


unsigned long time = micros();

void loop() {
  if (micros() > time + 1000000) {
    time = micros();
    start_command(volume_up);
  }
  
  signal_send();
  if (signal_recieve()) {
    decode_signal();
  }
}
