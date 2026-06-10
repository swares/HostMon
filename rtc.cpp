/* rtc.cpp — PCF85063 external RTC.
 *
 *  TEMPORARILY DISABLED (no I2C access here).
 *  -----------------------------------------------------------------------------
 *  This RTC sits on the same SDA8/SCL9 bus as the GT911 touch + I/O expander,
 *  which the ESP32_Display_Panel / ESP32_IO_Expander stack drives with the
 *  *legacy* IDF I2C driver (esp-idf/components/driver/i2c/i2c.c). Arduino's Wire
 *  uses the *new* driver_ng (esp32-hal-i2c-ng.c -> esp_driver_i2c/i2c_master.c).
 *  Linking BOTH drivers makes the new driver's startup guard
 *  (check_i2c_driver_conflict) abort the chip before setup() ever runs:
 *      "i2c: CONFLICT! driver_ng is not allowed to be used with this old driver"
 *
 *  To keep the firmware on a SINGLE I2C driver (the panel's legacy one), this
 *  module no longer uses Wire. Time is sourced from NTP, then runtime. The RTC
 *  will be re-implemented on the legacy driver (sharing the panel's bus) once the
 *  display is confirmed working.
 */
#include "rtc.h"
#include "config.h"

bool Rtc::begin(){
  Serial.println("[rtc] disabled (single-I2C-driver build); time via NTP/runtime");
  return false;
}
bool Rtc::present(){ return false; }
bool Rtc::get(struct tm&){ return false; }
bool Rtc::set(const struct tm&){ return false; }
void Rtc::releaseBus(){ /* no-op: RTC holds no I2C bus in this build */ }
