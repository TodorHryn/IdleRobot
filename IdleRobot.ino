#include <Adafruit_SSD1306.h>
#include <LowPower.h>
#include <EEPROM.h>
#include "CircularVector.hpp"
#include "SmallFloat.h"

#define LIGHT_SENSOR_PIN A6
#define BATTERY_PIN A3
#define DEBUG_PIN_1 2
#define DEBUG_PIN_2 3
#define LED_PIN 4
#define TURN_OFF_CHARGE 0.2
#define TURN_ON_CHARGE 0.4

#define ADC_REF_VOLTAGE 4.34
#define BATTERY_VOLTAGE_0 3.4
#define BATTERY_VOLTAGE_1 4.2

#define ERR_NO_BATTERY_VOLTAGE_READING  0x01
#define ERR_DEBUG_PIN_1_ALWAYS_ON       0x02
#define ERR_DEBUG_PIN_2_ALWAYS_ON       0x04

const uint32_t EEPROM_SETTINGS_KEY = 0x5acc0dd1;

Adafruit_SSD1306 display(128, 64, &Wire, -1);

#define MAX(a, b, c) ((a > b && a > c) ? a : (b > c ? b : c))

template<int L0, int L1, int L2>
class SmallNN
{
public:
  float toFloat(uint8_t val)
  {
    return static_cast<float>(val) / UINT8_MAX * (maxv - minv) + minv;
  }

  void runLayer(uint8_t size, uint8_t prevSize, uint8_t *w, uint8_t *a)
  {
    for (uint8_t j = 0; j < size; ++j)
    {
      float sum = toFloat(a[j]);
      for (uint8_t k = 0; k < prevSize; ++k)
        sum += inputs[k] * toFloat(w[j * prevSize + k]);
      outputs[j] = tanh(sum);
    }

    memcpy(inputs, outputs, sizeof(inputs));
  }

  void run()
  {
    runLayer(L1, L0, &w1[0][0], a1);
    runLayer(L2, L1, &w2[0][0], a2);
  }
  
  float minv = 1e9, maxv = -1e9;
  float inputs[MAX(L0, L1, L2)], outputs[MAX(L0, L1, L2)];
  uint8_t w1[L1][L0];
  uint8_t w2[L2][L1];
  uint8_t a1[L1];
  uint8_t a2[L2];
};

uint8_t brainsBin[] =  {41, 168, 152, 192, 30, 137, 23, 64, 131, 191, 207, 29, 254, 127, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 154, 145, 198, 1, 0, 0, 0, 0, 250, 75, 247, 127, 0, 0, 0, 176, 2, 0, 0, 0, 0, 0, 172, 86, 51, 168, 173, 186, 0, 8, 55, 122, 193, 153, 138, 173, 146, 73, 102, 130, 220, 117, 140, 125, 136, 146, 145, 231, 147, 173, 171, 158, 195, 170, 178, 143, 121, 143, 255, 157, 226, 187, 118, 161, 185, 177, 152, 0, 100, 0};
SmallNN<2, 7, 3> &brains = *((SmallNN<2, 7, 3>*) brainsBin);

float x = 0, y = 0;
float dirX = 1, dirY = 0.5;
uint32_t cycles = 0;
uint32_t cyclesTotal = 0;
bool energySaving = false;
bool screenOn = true;
CircularVector<SmallFloat<0, 1>> chargeLevel(50);
CircularVector<SmallFloat<0, 1>> lightLevel(10);
CircularVector<SmallFloat<0, 1>> hourChargeLevel(48);
float prevDayAvgCharge = 0;
float currDayAvgCharge = 0;
BoolCircularVector hourDebugPin1State(24), hourDebugPin2State(24);
uint32_t cyclesSinceDebugPin1On = 0, cyclesSinceDebugPin2On = 0;
bool prevDebugOn = false;

bool debugPinOk(const BoolCircularVector &v) {
  if (v.size() < 8)
    return true;
  uint8_t countOn = 0;
  for (uint8_t i = 0; i < v.size(); ++i)
    if (v.get(i))
      countOn++;
  return countOn * 3 < v.size();
}

template<uint16_t FROM, uint16_t TO>
float average(const CircularVector<SmallFloat<FROM, TO>> &v) {
  if (v.size() == 0)
    return 0;

  float sum = v[0];
  for (uint8_t i = 1; i < v.size(); ++i)
    sum += v[i];
  return sum / v.size();
}

void saveSettings() {
  uint16_t offset = 0;

  EEPROM.put(offset, EEPROM_SETTINGS_KEY);
  offset += 4;

  EEPROM.put(offset, cyclesTotal);
  offset += sizeof(cyclesTotal);

  uint8_t hourChargeLevelSize = hourChargeLevel.size();
  EEPROM.put(offset, hourChargeLevelSize);
  offset += sizeof(hourChargeLevelSize);

  for (uint8_t i = 0; i < hourChargeLevelSize; ++i) {
    EEPROM.put(offset, hourChargeLevel[i].m_data);
    offset += sizeof(hourChargeLevel[i].m_data);
  }

  EEPROM.put(offset, hourDebugPin1State.sz);
  offset += sizeof(hourDebugPin1State.sz);
  for (uint8_t i = 0; i < ceil(static_cast<float>(hourDebugPin1State.capacity) / 8); ++i) {
    EEPROM.put(offset, hourDebugPin1State.data[i]);
    offset += sizeof(hourDebugPin1State.data[i]);
  }
  EEPROM.put(offset, hourDebugPin2State.sz);
  offset += sizeof(hourDebugPin2State.sz);
  for (uint8_t i = 0; i < ceil(static_cast<float>(hourDebugPin2State.capacity) / 8); ++i) {
    EEPROM.put(offset, hourDebugPin2State.data[i]);
    offset += sizeof(hourDebugPin2State.data[i]);
  }

  EEPROM.put(offset, energySaving);
}

void readSettings() {
  uint16_t offset = 0;

  uint32_t settingsKey;
  EEPROM.get(offset, settingsKey);
  offset += 4;

  if (settingsKey == EEPROM_SETTINGS_KEY) {
    EEPROM.get(offset, cyclesTotal);
    offset += sizeof(cyclesTotal);

    uint8_t hourChargeLevelSize;
    EEPROM.get(offset, hourChargeLevelSize);
    offset += sizeof(hourChargeLevelSize);

    for (uint8_t i = 0; i < hourChargeLevelSize; ++i) {
      SmallFloat<0, 1> charge;
      EEPROM.get(offset, charge.m_data);
      hourChargeLevel.push(charge);
      offset += sizeof(charge.m_data);
    }

    EEPROM.get(offset, hourDebugPin1State.sz);
    offset += sizeof(hourDebugPin1State.sz);
    for (uint8_t i = 0; i < ceil(static_cast<float>(hourDebugPin1State.capacity) / 8); ++i) {
      EEPROM.get(offset, hourDebugPin1State.data[i]);
      offset += sizeof(hourDebugPin1State.data[i]);
    }
    EEPROM.get(offset, hourDebugPin2State.sz);
    offset += sizeof(hourDebugPin2State.sz);
    for (uint8_t i = 0; i < ceil(static_cast<float>(hourDebugPin2State.capacity) / 8); ++i) {
      EEPROM.get(offset, hourDebugPin2State.data[i]);
      offset += sizeof(hourDebugPin2State.data[i]);
    }

    EEPROM.get(offset, energySaving);
  }
}

void setup() {
  CLKPR = 0x80;
  CLKPR = 0x01;

  readSettings();
  updateAvgCharge();

  for (int i = 2; i <= 13; ++i)
    pinMode(i, INPUT);
  for (int i = A0; i <= A7; ++i)
    pinMode(i, INPUT);

  analogReference(DEFAULT);
  pinMode(LIGHT_SENSOR_PIN, INPUT);
  pinMode(BATTERY_PIN, INPUT);
  pinMode(DEBUG_PIN_1, INPUT_PULLUP);
  pinMode(DEBUG_PIN_2, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    for (;;);
  }

  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.clearDisplay();
  display.display();
}

void setScreenBrightness(float val) {
  display.ssd1306_command(SSD1306_SETPRECHARGE);
  display.ssd1306_command(0x10);
  display.ssd1306_command(SSD1306_SETCONTRAST);
  display.ssd1306_command(0x01 + static_cast<int>(0xFE * val));
}

float mapf(float val, float fromLow, float fromHigh, float toLow, float toHigh) {
  float ret = (val - fromLow) / (fromHigh - fromLow) * (toHigh - toLow) + toLow;
  return min(toHigh, max(toLow, ret));
}

void render(float lightLevel, bool debugMode, uint8_t errorCodes) {
  float avgChargeLevel = average(chargeLevel);

  //Calc if should be energy saving
  bool turnScreenOff = energySaving;
  bool turnScreenOn = false;
  if (avgChargeLevel <= TURN_OFF_CHARGE && chargeLevel.size() >= 5) {
    energySaving = true;
    turnScreenOff = true;
  }
  else if (avgChargeLevel >= TURN_ON_CHARGE && chargeLevel.size() >= 5) {
    energySaving = false;
    turnScreenOn = true;
  }

  if (debugMode) {
    turnScreenOff = false;
    turnScreenOn = true;
  }

  if (screenOn && turnScreenOff) {
    display.clearDisplay();
    display.display();
    display.ssd1306_command(SSD1306_DISPLAYOFF);
    screenOn = false;
  }
  else if (!screenOn && turnScreenOn) {
    display.ssd1306_command(SSD1306_DISPLAYON);
    screenOn = true;
  }

  //Draw
  if (screenOn) {
    digitalWrite(LED_PIN, LOW);
    display.clearDisplay();
    display.setCursor(0, 0);

    if (debugMode) {
      display.print(F("Cur. on: "));
      display.print(String((cycles / 450) / 24));
      display.print(F(" d "));
      display.print(String((cycles / 450) % 24)); 
      display.println(F(" h"));
      
      display.print(F("Tot. on: "));
      display.print(String((cyclesTotal / 450) / 24));
      display.print(F(" d "));
      display.print(String((cyclesTotal / 450) % 24));
      display.println(F(" h"));
      
      display.println(String(avgChargeLevel * 100) + "%");
      
      uint8_t pc = prevDayAvgCharge * 100;
      uint8_t cc = currDayAvgCharge * 100;
      display.print(F("YDA "));
      display.print(String(pc));
      display.print(F("% TDA "));
      display.print(String(cc));
      display.println(F("%"));
      display.print(F("Light "));
      display.println(lightLevel);

      display.print(F("E "));
      display.print(String(brains.outputs[0]));
      display.print(F(" H "));
      display.print(String(brains.outputs[1]));
      display.print(F(" B "));
      display.println(String(brains.outputs[2]));

      if (errorCodes & ERR_NO_BATTERY_VOLTAGE_READING)
        display.println(F("<ERR> Unknown batt. V"));
      if (errorCodes & ERR_DEBUG_PIN_1_ALWAYS_ON)
        display.println(F("<ERR> Dbg 1 always on"));
      if (errorCodes & ERR_DEBUG_PIN_2_ALWAYS_ON)
        display.println(F("<ERR> Dbg 2 always on"));
    }
    else {
      brains.inputs[0] = lightLevel;
      brains.inputs[1] = constrain((currDayAvgCharge - prevDayAvgCharge) * 100, -1.0f, 1.0f);
      brains.run();
      
      // ***** Mouth *****
      display.drawFastHLine(55, 63, 18, SSD1306_WHITE);

      if (brains.outputs[1] >= -1/3.0f) {
        display.drawPixel(53, 62, SSD1306_WHITE);
        display.drawPixel(54, 62, SSD1306_WHITE);
        display.drawPixel(55, 62, SSD1306_WHITE);
        display.drawPixel(127 - 55, 62, SSD1306_WHITE);
        display.drawPixel(127 - 54, 62, SSD1306_WHITE);
        display.drawPixel(127 - 53, 62, SSD1306_WHITE);
      }

      if (brains.outputs[1] >= 1/3.0f) {
        display.drawPixel(52, 61, SSD1306_WHITE);
        display.drawPixel(53, 61, SSD1306_WHITE);
        display.drawPixel(127 - 53, 61, SSD1306_WHITE);
        display.drawPixel(127 - 52, 61, SSD1306_WHITE);
        display.drawPixel(51, 60, SSD1306_WHITE);
        display.drawPixel(52, 60, SSD1306_WHITE);
        display.drawPixel(127 - 52, 60, SSD1306_WHITE);
        display.drawPixel(127 - 51, 60, SSD1306_WHITE);
        display.drawPixel(50, 59, SSD1306_WHITE);
        display.drawPixel(51, 59, SSD1306_WHITE);
        display.drawPixel(127 - 51, 59, SSD1306_WHITE);
        display.drawPixel(127 - 50, 59, SSD1306_WHITE);
      }

      // ****** Eyes ******
      if (brains.outputs[0] >= 0.5) {
        display.drawCircle(32, 28, 28, SSD1306_WHITE);
        display.drawCircle(127 - 32, 28, 28, SSD1306_WHITE);
        display.fillCircle(32 + x, 28 + y, 6, SSD1306_WHITE);
        display.fillCircle(127 - 32 + x, 28 + y, 6, SSD1306_WHITE);
      }
      else {
        display.drawFastHLine(32 - 14, 28, 28, SSD1306_WHITE);
        display.drawFastHLine(127 - 32 - 14, 28, 28, SSD1306_WHITE);
      }

      //where eyes look at
      x += dirX;
      y += dirY;
      if (x >= 10)
        dirX = -random(0, 100) / 100.0;
      else if (x <= -10)
        dirX = random(0, 100) / 100.0;
      if (y >= 10)
        dirY = -random(0, 100) / 100.0;
      else if (y <= -10)
        dirY = random(0, 100) / 100.0;
    }

    setScreenBrightness(constrain(brains.outputs[2], 0, 1));
    display.display();
  }
  else {
    digitalWrite(LED_PIN, HIGH);
  }
}

void updateAvgCharge() {
  prevDayAvgCharge = 0;
  currDayAvgCharge = 0;

  if (hourChargeLevel.size() == 48) {
    for (uint8_t i = 0; i < 24; ++i) {
      prevDayAvgCharge += hourChargeLevel[i];
      currDayAvgCharge += hourChargeLevel[i + 24];
    }
    prevDayAvgCharge /= 24;
    currDayAvgCharge /= 24;
  }
}

void loop() {
  uint8_t errorCodes = 0;

  bool debugMode = false;
  bool debugPin1 = digitalRead(DEBUG_PIN_1) == LOW;
  bool debugPin2 = digitalRead(DEBUG_PIN_2) == LOW;
  if (debugPin1)
    cyclesSinceDebugPin1On = 0;
  else
    cyclesSinceDebugPin1On++;
  if (debugPin2)
    cyclesSinceDebugPin2On = 0;
  else
    cyclesSinceDebugPin2On++;
  if (debugPinOk(hourDebugPin1State))
    debugMode = debugMode || debugPin1;
  else
    errorCodes = errorCodes | ERR_DEBUG_PIN_1_ALWAYS_ON;
  if (debugPinOk(hourDebugPin2State))
    debugMode = debugMode || debugPin2;
  else
    errorCodes = errorCodes | ERR_DEBUG_PIN_2_ALWAYS_ON;

  //light level from 0 (no light) to 1 (max light)
  lightLevel.push(max(0, 700 - analogRead(LIGHT_SENSOR_PIN)) / 700.0);

  //I know charge level depends on voltage non-linearly, but it doesn't matter here
  float charge = mapf(analogRead(BATTERY_PIN), 0, 1023, 0, ADC_REF_VOLTAGE);
  if (charge > 2 && charge < ADC_REF_VOLTAGE * 0.99) {
    charge = mapf(charge, BATTERY_VOLTAGE_0, BATTERY_VOLTAGE_1, 0, 1);
    chargeLevel.push(charge);
  }
  else
    errorCodes = errorCodes | ERR_NO_BATTERY_VOLTAGE_READING;
  if (cyclesTotal % 450 == 0) {
    hourChargeLevel.push(average(chargeLevel));
    updateAvgCharge();
    hourDebugPin1State.push(cyclesSinceDebugPin1On < 450);
    hourDebugPin2State.push(cyclesSinceDebugPin2On < 450);
  }

  if ((debugMode && !prevDebugOn) || (!debugMode && prevDebugOn)) {
    digitalWrite(LED_PIN, LOW);
    delay(250);
    digitalWrite(LED_PIN, HIGH);
    delay(250);
    digitalWrite(LED_PIN, LOW);
  }
  prevDebugOn = debugMode;

  render(average(lightLevel), debugMode, errorCodes);

  cycles++;
  cyclesTotal++;
  if (cyclesTotal % 100 == 0)
    saveSettings();

  LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_ON);
}
