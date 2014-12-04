/* 
 
 Simple heating thermostat using DS18B20 and 3-digits 7-segment display (common anode). 
 It has basic operation with only two buttons.
 
 A0 - DS18B20
 A1 - "-" button
 A2 - "+" button
 A3 - Relay output. High to activate heater.
 D6-D13 - Display segments (a, b, c, d, e, f, g and point).
 D3-D5 - Digits selector.
 
 Use cases:
 1. Press "+" or "-" to view/change target temperature.
 2. Press both "+" and "-" to view/change histeresis temperature.
 
 */
#include "SevSeg.h"
#include <OneWire.h>
#include <EEPROM.h>

struct eeStruct { 
  float temp; 
  float histeresis; 
};
typedef struct eeStruct ee_t;

SevSeg sevseg;
float _sensTemp[5] = { 
  -111 }; // Magic value for uninitialized values.
byte _sensCount = 0;

// Default target temp is 22.0 and 0.5 degrees for histeresis.
ee_t ee = {
  22.0, 0.5}; 

int EEPROM_writeAnything(int ee, const void* value, int sizeValue)
{
  const byte* p = (const byte*)value;
  int i;
  for (i = 0; i < sizeValue; i++)
    EEPROM.write(ee++, *p++);
  return i;
}

int EEPROM_readAnything(int ee, void* value, int sizeValue)
{
  byte* p = (byte*)value;
  int i;
  for (i = 0; i < sizeValue; i++)
    *p++ = EEPROM.read(ee++);
  return i;
}

void setup() {
  EEPROM_readAnything(0, &ee, sizeof(ee));

  if (ee.temp <= 0.0 || ee.temp > 50.0)
    ee.temp = 22.0;

  if (ee.histeresis <= 0.1 || ee.histeresis > 5.0)
    ee.histeresis = 0.5;

  // Pin "-"
  pinMode(A1, INPUT);
  digitalWrite(A1, HIGH);

  // Pin "+"
  pinMode(A2, INPUT);
  digitalWrite(A2, HIGH);

  // Relay out
  pinMode(A3, OUTPUT);
  digitalWrite(A3, HIGH);

  // Common anode display
  sevseg.Begin(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13);
  sevseg.Brightness(100);

  searchSensors();
  updateTemp();

  byte d = 0;
  unsigned long welcomeTime = millis();
  while (_sensTemp[0] == -111.0 || (int)_sensTemp[0] == 85) { 
    sevseg.NewNum(888, d);
    if ((millis() - welcomeTime) > 300) {
      d++;
      if (d == 3)
        d = 0;
      welcomeTime = millis();
    }
    sevseg.PrintOutput();
    updateTemp();
  }
}

int btnPlusState = HIGH;
int btnPlusLastState = HIGH;
long btnPlusLastDebounceTime = 0;

int btnMinusState = HIGH;
int btnMinusLastState = HIGH;
long btnMinusLastDebounceTime = 0;

long debounceDelay = 50;

long _stateTime = 0;
long _lastStateTime = 0;
int _state = 0;
long _stateBlinkLastTime = 0;
boolean _printOutput = true;

long _lastValueUpdate = 0;

long _lastEeChanged = 0;

void loop() {
  updateBtnPlus();
  updateBtnMinus();

  if (_state == 0) {
    if (btnPlusState == LOW && btnMinusState == LOW) {
      _state = 2; // Show-edit histeresis.
      _stateTime = millis();
      _lastStateTime = millis();
      _stateBlinkLastTime = millis();
    } 
    else if (btnPlusState == LOW) {
      _state = 1;
      _stateTime = millis();
      _lastStateTime = millis();
      _stateBlinkLastTime = millis();
    } 
    else if (btnMinusState == LOW) {
      _state = 1;
      _stateTime = millis();
      _lastStateTime = millis();
      _stateBlinkLastTime = millis();
    }
  } 
  else {
    if ((millis() - _stateBlinkLastTime) > 250) {
      _printOutput = !_printOutput;
      _stateBlinkLastTime = millis();
    }

    if ((millis() - _stateTime) > 3000) {
      _state = 0;
      _printOutput = true;
    } 
    else {
      if (_state == 1) {
        if (btnPlusState == LOW && btnMinusState == LOW) {
          _state = 2; // Show-edit histeresis.
          _stateTime = millis();
          _lastStateTime = millis();
          _stateBlinkLastTime = millis();
        }
      }

      if ((millis() - _lastStateTime) > 250) { // Prevent immediate change to values.
        boolean needValueUpdate = (millis() - _lastValueUpdate) > 250;
        if (needValueUpdate) {
          _lastValueUpdate = millis();
        }

        if (btnPlusState == LOW) {
          _stateTime = millis();
          if (needValueUpdate) {
            if (_state == 1) {
              ee.temp += 0.1;
              _lastEeChanged = _stateTime;
            } 
            else if (_state == 2) {
              ee.histeresis += 0.1;
              _lastEeChanged = _stateTime;
            }
          }
        } 
        else if (btnMinusState == LOW) {
          _stateTime = millis();
          if (needValueUpdate) {
            if (_state == 1) {
              ee.temp -= 0.1;
              _lastEeChanged = _stateTime;
            }
            else if (_state == 2) {
              ee.histeresis -= 0.1;
              _lastEeChanged = _stateTime;
            }
          }
        }

        fixMinMaxValues();
      }
    }
  }

  updateLcd();

  if (_state == 0)
    updateRelay();

  long eeChanged = millis() - _lastEeChanged;
  if (eeChanged > 2000 && _lastEeChanged != 0) {
    EEPROM_writeAnything(0, &ee, sizeof(ee));
    _lastEeChanged = 0;
  }
}

void fixMinMaxValues()
{
  if (ee.temp < 0)
    ee.temp = 0;

  if (ee.temp > 50)
    ee.temp = 50;

  if (ee.histeresis < 0.1)
    ee.histeresis = 0.1;

  if (ee.histeresis > 5.0)
    ee.histeresis = 5.0;
}

long _lastRelaySwitched = 0;
int _lastRelayState = HIGH;

void updateRelay()
{
  if ((millis() - _lastRelaySwitched) < 60000)
    return;

  float currTemp = _sensTemp[0];
  if (currTemp == 0)
    return;

  float highTemp = ee.temp + ee.histeresis;
  float lowTemp = ee.temp - ee.histeresis;

  int newState = _lastRelayState;  
  if (currTemp >= highTemp)
  {
    newState = HIGH; // Turn off;
  } 
  else if (currTemp <= lowTemp) {
    newState = LOW;
  }

  if (newState != _lastRelayState)
  {
    digitalWrite(A3, newState);
    _lastRelayState = newState;
    _lastRelaySwitched = millis();
  }
}

void updateLcd()
{
  switch (_state)
  {
  case 0:
    {
      updateTemp();
      float temp = _sensTemp[0];
      sevseg.NewNum(temp * 10, (byte) 1); 
    }
    break;

  case 1:
    sevseg.NewNum(ee.temp * 10, (byte) 1);
    break;

  case 2:
    sevseg.NewNum(ee.histeresis * 10, (byte) 1);
    break;
  }

  if (_printOutput)
    sevseg.PrintOutput();
}

void updateBtnPlus()
{
  // read the state of the switch into a local variable:
  int reading = digitalRead(A2);

  // check to see if you just pressed the button
  // (i.e. the input went from LOW to HIGH),  and you've waited
  // long enough since the last press to ignore any noise:  

  // If the switch changed, due to noise or pressing:
  if (reading != btnPlusLastState) {
    // reset the debouncing timer
    btnPlusLastDebounceTime = millis();
  }

  if ((millis() - btnPlusLastDebounceTime) > debounceDelay) {
    // whatever the reading is at, it's been there for longer
    // than the debounce delay, so take it as the actual current state:

    // if the button state has changed:
    if (reading != btnPlusState) {
      btnPlusState = reading;
    }
  }

  // save the reading.  Next time through the loop,
  // it'll be the lastButtonState:
  btnPlusLastState = reading;
}

void updateBtnMinus()
{
  // read the state of the switch into a local variable:
  int reading = digitalRead(A1);

  // check to see if you just pressed the button
  // (i.e. the input went from LOW to HIGH),  and you've waited
  // long enough since the last press to ignore any noise:  

  // If the switch changed, due to noise or pressing:
  if (reading != btnMinusLastState) {
    // reset the debouncing timer
    btnMinusLastDebounceTime = millis();
  }

  if ((millis() - btnMinusLastDebounceTime) > debounceDelay) {
    // whatever the reading is at, it's been there for longer
    // than the debounce delay, so take it as the actual current state:

    // if the button state has changed:
    if (reading != btnMinusState) {
      btnMinusState = reading;
    }
  }

  // save the reading.  Next time through the loop,
  // it'll be the lastButtonState:
  btnMinusLastState = reading;
}

// DS18B20 attached to A0
OneWire _ds(A0);
unsigned long _sensLastUpdated[5] = { 
  0, 0, 0, 0, 0 };
byte _sensCurr = 0;
unsigned long _sensRead = 0;
int _sensUpdateInterval = 5000; // in ms.
byte _sens[5][8] = { 
  0 };

void searchSensors()
{
  _ds.reset_search();
  byte addr[8];
  int i = 0;
  while (_ds.search(addr))
  {
    if (OneWire::crc8(addr, 7) == addr[7]) // CRC is valid.
    {
      for (int j = 0; j < 8; j++)
        _sens[i][j] = addr[j];
      i++;
    }
    //else
    //{
    //  Serial.println("CRC is not valid. Skip sensor.");
    //}
  }
  _sensCount = i;
  _ds.reset_search();

  //Serial.print("Found ");
  //Serial.println(_sensCount);
}

void updateTemp()
{
  for (int sensNum = 0; sensNum < _sensCount; sensNum++)
  {
    if (_sensCurr != 255 && _sensCurr != sensNum)
      return;

    unsigned long time = millis();

    if (_sensCurr == 255 && (time - _sensLastUpdated[sensNum]) < _sensUpdateInterval)
      return;

    if (_sensCurr == 255)
    {
      _ds.reset();
      _ds.select(_sens[sensNum]);
      _ds.write(0x44, 1);         // start conversion, with parasite power on at the end

      // lock bus
      _sensRead = time;
      _sensCurr = sensNum;
    }
    else if (_sensCurr == sensNum && (time - _sensRead) >= 800)
    {
      byte present = _ds.reset();
      _ds.select(_sens[sensNum]);
      _ds.write(0xBE);         // Read Scratchpad

      byte data[9];
      for (int i = 0; i < 9; i++) {           // we need 9 bytes
        data[i] = _ds.read();
      }

      byte computedCrc = OneWire::crc8( data, 8);
      if (computedCrc != data[8])
      {
        _sensLastUpdated[sensNum] = time - _sensUpdateInterval / 2;

        // release bus
        _sensCurr = 255;
        _sensRead = 0;
        return;
      }

      // convert the data to actual temperature
      unsigned int raw = (data[1] << 8) | data[0];
      byte cfg = (data[4] & 0x60);
      if (cfg == 0x00) raw = raw << 3;  // 9 bit resolution, 93.75 ms
      else if (cfg == 0x20) raw = raw << 2; // 10 bit res, 187.5 ms
      else if (cfg == 0x40) raw = raw << 1; // 11 bit res, 375 ms
      // default is 12 bit resolution, 750 ms conversion time

      _sensTemp[sensNum] = (float)raw / 16.0;
      _sensLastUpdated[sensNum] = time;

      // release bus
      _sensCurr = 255;
      _sensRead = 0;
    }
  }
}


