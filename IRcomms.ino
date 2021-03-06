//The IR communication components.
//
// Hacked together by Andrew Shirley

// Code which was either 'copypasta'ed or used as inspiration at some point:
//
// http://www.arduino.cc/cgi-bin/yabb2/YaBB.pl?num=1240843250 Copyright (C) Timo Herva aka 'mettapera', 2009
// http://tthheessiiss.wordpress.com/2009/08/05/dirt-cheap-wireless/ 

//NB. as these are quite time intensive enabling this often breaks it :-(
//#define DEBUG_SEND 1
//#define DEBUG_RECV 1

#define altfire_pin 2
#define trigger_pin 3
#define torchred_pin 4
#define torchgreen_pin 5
#define torchblue_pin 6
#define laser_pin 7
#define pin_ir_reciever 8
#define pin_infrared 9
#define power_relay_pin 10
#define muzzlered_pin 11
#define muzzleblue_pin 12
#define muzzlegreen_pin 13
#define power_monitor_pin A0

// some timings
long headerDuration = 2400;
long intervalDuration = 600;
long oneDuration = 1200;
long zeroDuration = 600;
long postDataDuration = 200000; //must be at least intervalDuration + timingTolerance * 2

byte timingTolerance = 100;

////////////////////////
// IR Writing variables
unsigned long writeBuffer = 0;
byte writeBits = 0;
unsigned long writeUpTime = 0;
unsigned long writeDownTime = 0;
unsigned long writeLastChangeTime = 0;
unsigned long postDataTime = 0;

////////////////////////
// IR reading variables
unsigned long readBuffer = 0;
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
  boolean pinValue = ! digitalRead(pin_ir_reciever);

  if (!oldPinValue && pinValue) {
    //IR rising edge
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
#ifdef DEBUG_RECV
      Serial.println("--");
#endif
    }
    else if (within_tolerance(duration, oneDuration, timingTolerance)) {
      //we are within tolerance of 1200 us - a one
      readBuffer = (readBuffer << 1) + 1;
      bitsRead++;
#ifdef DEBUG_RECV
      Serial.println("-1");
#endif
    }
    else if (within_tolerance(duration, zeroDuration, timingTolerance)) {
      //we are within tolerance of 600 us - a zero
      readBuffer = readBuffer << 1;
      bitsRead++;
#ifdef DEBUG_RECV
      Serial.println("-0");
#endif
    }
    else {
#ifdef DEBUG_RECV
      Serial.print("/ @");
      Serial.print(readFallTime);
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
#ifdef DEBUG_RECV
      Serial.println("xx");
#endif
      return 1;
    }
    else {
      //still low, waiting for the interval
      return 0;
    }
  }
}

void finished_signal_decode() {
  bitsRead = -1;
}

boolean within_tolerance(unsigned long value, unsigned long target, byte tolerance) {
  long remainder = value - target;
  return remainder < tolerance && remainder > -tolerance;
}

////////////////////////
// IR writing functions

void start_command(unsigned long command, byte myTeamId) {
  if (writeUpTime || writeDownTime || postDataTime) {
    //already writing ignore this
    return;
  }

  command = reverse(command, 16);
  writeBuffer = addParityBit(command);
  writeBits = 17;

  digitalWrite(laser_pin, HIGH);
  muzzleflash_up(myTeamId);

  //write header
  ir_up();
#ifdef DEBUG_SEND
  Serial.println("  \\");
#endif
  writeDownTime = headerDuration;
}

void signal_send() {
  unsigned long elapsed = micros() - writeLastChangeTime;

  if (postDataTime && postDataTime <= elapsed) {
    digitalWrite(laser_pin, LOW);
    postDataTime = 0;
  }
  else if (writeDownTime && writeDownTime <= elapsed) {
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
    else {
      muzzleflash_down();
      postDataTime = postDataDuration;
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
  TCCR1A = _BV(COM1A0);
  writeLastChangeTime = micros();
}

void ir_down() {
  TCCR1A = 0;
  digitalWrite(pin_infrared, LOW);
  writeLastChangeTime = micros();
}

/*
 * Reverse the num least significant bits.
 */
unsigned long reverse(unsigned long in, int num) {
  unsigned long out = 0;
  for (int i = 0; i < num; i++) {
    out = out << 1;
    out = out | (in & 1); //take the lsb from in to out
    in = in >> 1;
  }
  
  return out;
}

unsigned long timeCache = 0;

//these are set by the serial code to say whther we have just written (or read) from the serial line.
boolean serialWritten = false;
boolean serialRead = false;

void timeDebug() {
  if (timeCache == 0) {
    timeCache = micros();
  }
  else {
    int diff = micros() - timeCache;
    if (diff > 500) {
      Serial.print(diff);
      //some status flags as well
      if (writeUpTime || writeDownTime || postDataTime) {
        //in the middle of sending IR!
        Serial.print("S");
      }
      else {
        Serial.print(" ");
      }

      if (bitsRead > -1) {
        //in the middle of receiving IR!
        Serial.print("R");
      }
      else {
        Serial.print(" ");
      }
      
      if (serialWritten) {
        Serial.print("s");
      }
      else {
        Serial.print(" ");
      }
      
      if (serialRead) {
        Serial.print("r");
      }
      else {
        Serial.print(" ");
      }
      
      Serial.println();
    }
    timeCache = 0;
  }
}

void muzzleflash_up(int flashteam) {
  switch (flashteam) {
    case 1: //red
      digitalWrite(muzzlered_pin, HIGH);
      break;
    case 2: //green
      digitalWrite(muzzlegreen_pin, HIGH);
      break;
    case 3: //blue
      digitalWrite(muzzleblue_pin, HIGH);
      break;
    case 4: //yellow
      digitalWrite(muzzlered_pin, HIGH);
      digitalWrite(muzzlegreen_pin, HIGH);
      break;
    case 5: //purple
      digitalWrite(muzzlered_pin, HIGH);
      digitalWrite(muzzleblue_pin, HIGH);
      break;
    case 6: //cyan
      digitalWrite(muzzlegreen_pin, HIGH);
      digitalWrite(muzzleblue_pin, HIGH);
      break;
    case 7: //white
      digitalWrite(muzzlered_pin, HIGH);
      digitalWrite(muzzlegreen_pin, HIGH);
      digitalWrite(muzzleblue_pin, HIGH);
      break;
  }
  //?? run out of digital combinations
}

void muzzleflash_down() {
  digitalWrite(muzzlered_pin, LOW);
  digitalWrite(muzzlegreen_pin, LOW);
  digitalWrite(muzzleblue_pin, LOW);
}

void torch_up(int flashteam) {
  switch (flashteam) {
    case 1: //red
      digitalWrite(torchred_pin, HIGH);
      break;
    case 2: //green
      digitalWrite(torchgreen_pin, HIGH);
      break;
    case 3: //blue
      digitalWrite(torchblue_pin, HIGH);
      break;
    case 4: //yellow
      digitalWrite(torchred_pin, HIGH);
      digitalWrite(torchgreen_pin, HIGH);
      break;
    case 5: //purple
      digitalWrite(torchred_pin, HIGH);
      digitalWrite(torchblue_pin, HIGH);
      break;
    case 6: //cyan
      digitalWrite(torchgreen_pin, HIGH);
      digitalWrite(torchblue_pin, HIGH);
      break;
    case 7: //white
      digitalWrite(torchred_pin, HIGH);
      digitalWrite(torchgreen_pin, HIGH);
      digitalWrite(torchblue_pin, HIGH);
      break;
  }
  //?? run out of digital combinations
}

void torch_down() {
  digitalWrite(torchred_pin, LOW);
  digitalWrite(torchgreen_pin, LOW);
  digitalWrite(torchblue_pin, LOW);
}
