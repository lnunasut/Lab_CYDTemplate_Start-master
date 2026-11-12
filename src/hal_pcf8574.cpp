#include <Wire.h>

#include "board_config.h"
#include "hal.h"

namespace hal {

namespace {

uint8_t gOutputMask = 0;
bool gPresent = false;
uint32_t gLastProbe = 0;

uint8_t portValue()
{
    // PCF8574 is quasi-bidirectional: write 1 to release an input pin.
    // LED circuits are current sinks, so a LOW output turns an LED on.
    return static_cast<uint8_t>(0xF0 | ((~gOutputMask) & 0x0F));
}

bool reconnectIfDue()
{
    if (gPresent) return true;
    const uint32_t now = millis();
    if (now - gLastProbe < 1000) return false;
    gLastProbe = now;
    Wire.beginTransmission(cyd::PCF8574_ADDRESS);
    if (Wire.endTransmission() != 0) return false;
    Wire.beginTransmission(cyd::PCF8574_ADDRESS);
    Wire.write(portValue());
    gPresent = Wire.endTransmission() == 0;
    if (gPresent) Serial.println("[PCF8574] reconnected");
    return gPresent;
}

}  // namespace

void pcfBegin()
{
    Wire.begin(cyd::I2C_PIN_SDA, cyd::I2C_PIN_SCL);
    Wire.setClock(100000);
    gLastProbe = millis();
    Wire.beginTransmission(cyd::PCF8574_ADDRESS);
    gPresent = Wire.endTransmission() == 0;
    gOutputMask = 0;  // LEDs off at startup
    if (gPresent) {
        Wire.beginTransmission(cyd::PCF8574_ADDRESS);
        Wire.write(portValue());
        gPresent = Wire.endTransmission() == 0;
    }
    Serial.printf("[PCF8574] addr=0x%02X SDA=%u SCL=%u %s\n", cyd::PCF8574_ADDRESS, cyd::I2C_PIN_SDA, cyd::I2C_PIN_SCL,
                  gPresent ? "ready" : "not found");
}

bool pcf8574Present()
{
    return gPresent;
}

bool pcf8574Read(uint8_t &pins)
{
    if (!reconnectIfDue() || Wire.requestFrom(static_cast<uint8_t>(cyd::PCF8574_ADDRESS), static_cast<uint8_t>(1)) != 1) {
        gPresent = false;
        return false;
    }
    pins = static_cast<uint8_t>(Wire.read());
    return true;
}

bool pcf8574WriteOutputs(uint8_t onMask)
{
    gOutputMask = onMask & 0x0F;
    if (!gPresent) return false;
    Wire.beginTransmission(cyd::PCF8574_ADDRESS);
    Wire.write(portValue());
    gPresent = Wire.endTransmission() == 0;
    return gPresent;
}

uint8_t pcf8574OutputMask()
{
    return gOutputMask;
}

}  // namespace hal
