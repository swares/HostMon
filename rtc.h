/* ============================================================================
 *  rtc.h — PCF85063 external RTC (I2C) on the Waveshare 4.3" board.
 *  Shares the I2C bus with the touch controller / IO expander.
 * ========================================================================== */
#pragma once
#include <Arduino.h>
#include <time.h>

namespace Rtc {
  bool begin();                 // probe the chip; true if present
  bool present();               // was it found at begin()?
  bool get(struct tm& out);     // read time; false if absent or oscillator-invalid
  bool set(const struct tm& t); // write time (clears the low-voltage/stop flag)
  void releaseBus();            // hand the shared I2C pins to the display panel.
                                // After this, Wire is NOT used again (the panel's
                                // legacy I2C driver owns the bus; mixing drivers
                                // aborts). All RTC ops become no-ops once latched.
}
