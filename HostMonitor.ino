/* ============================================================================
 *  Host Monitor — Waveshare ESP32-S3-Touch-LCD-4.3 (800x480 parallel RGB)
 *  Arduino IDE sketch — main entry point
 *
 *  BOOT MODEL (fixes the white-screen / reboot loop):
 *    setup() does only fast, display-critical work, then returns so loop()
 *    starts pumping LVGL immediately (otherwise the panel shows an unrefreshed
 *    white framebuffer). The slow work — SD read, Wi-Fi join, and the one-time
 *    self-signed-cert generation — runs on a background "init" task. The core
 *    watchdog is disabled so the heavy RSA keygen can finish without a reset.
 *
 *  NOTE: this board uses a TCA9554PWR I/O expander @ 0x20 (NOT CH422G @ 0x24).
 *  The scanI2C() printout at boot confirms the real bus map.
 * ========================================================================== */
#include <Arduino.h>
#include <esp_task_wdt.h>
// NOTE: <Wire.h> deliberately NOT included. Arduino Wire pulls in the new
// (driver_ng) I2C driver; the display panel/expander stack uses the legacy IDF
// I2C driver. Linking both aborts at boot (check_i2c_driver_conflict). Keeping
// this sketch Wire-free leaves a single I2C driver in the binary.
#include "config.h"
#include "display.h"
#include "store.h"
#include "csv.h"
#include "settings.h"
#include "scheduler.h"
#include "notifier.h"
#include "wifi_portal.h"
#include "webserver.h"
#include "tls_gate.h"
#include "ui.h"

// setup() runs on the Arduino loopTask. The big stack was only for the on-device
// RSA keygen — which is gone now (the TLS cert is embedded), so 16KB is plenty
// for the WiFi/LVGL/web init. Shrinking it back frees internal heap so the check
// task's stack can actually be allocated (a 32KB loop stack was starving it).
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

static bool g_displayOk = false;

// (The Wire-based I2C scan was removed: Arduino Wire links the new I2C driver,
// which conflicts with the panel's legacy driver and aborts at boot. The panel
// library owns the bus; a legacy-driver scan can be re-added later if needed.)

// Init progress, in RTC RAM so it SURVIVES a crash/reset — the Setup card shows
// the last value, i.e. exactly which init step faulted in a reset loop.
//   1=sd 2=wifi 3=web(TLS) 4=notify 5=sched 9=ready
RTC_NOINIT_ATTR volatile int g_bootStage;

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[HostMonitor] booting...");
  Serial.printf("[HostMonitor] firmware %s, build %s %s\n", FIRMWARE_VERSION, __DATE__, __TIME__);

  // The RSA keygen and RGB panel DMA can starve the idle task; turn off the
  // Task Watchdog so they don't reset the board. Remove the loop task, then
  // DEINIT the whole TWDT — deinit unsubscribes the idle tasks itself. We must
  // NOT call disableCore0/1WDT() first: that deletes the idle entries, then
  // deinit's own unsubscribe_idle fails its ESP_ERROR_CHECK (ESP_ERR_NOT_FOUND)
  // and aborts. Deinit also removes the idle hook, so there's no
  // "task_wdt: ... task not found" flood afterward.
  disableLoopWDT();
  esp_task_wdt_deinit();   // unsubscribes idle tasks + tears down the TWDT

  Settings::begin();     // NVS config
  TlsGate::begin();      // create the one-TLS-session-at-a-time mutex before any task
                         // that can start a TLS handshake (web server / check engine)

  // Seed the clock from the external RTC FIRST, while Wire (new I2C driver) still
  // owns the bus. startClock() releases the bus afterward so the display panel's
  // legacy I2C driver can claim the same SDA8/SCL9 pins without a driver-conflict
  // abort ("CONFLICT! driver_ng is not allowed to be used with this old driver").
  Settings::startClock();           // seed system clock from RTC, then release I2C

  // ALL flash-writing bring-up happens HERE, before the RGB panel turns on. On
  // the ESP32-S3 a flash write disables the cache; the panel's refresh ISR is in
  // flash (CONFIG_LCD_RGB_ISR_IRAM_SAFE not set in the stock core), so if it fires
  // mid-write the chip faults — corrupting the screen and rebooting. With the
  // display still OFF these writes are safe, and later boots reuse the cached
  // results (FS already formatted, WiFi PHY-cal stored, TLS cert cached) so the
  // writes don't recur and the live display stays stable.
  //   - LittleFS mount/format          (first boot: format = long flash write)
  //   - WiFi bring-up                  (first boot: PHY calibration -> NVS)
  //   - Web/HTTPS + self-signed cert   (first boot: RSA keygen ~60s + cache write)
  // NOTE: FIRST boot sits on a BLACK screen for ~60-90s during RSA keygen. This is
  // one-time; subsequent boots come up in a couple seconds.
  Storage::beginSD();
  // Load saved hosts NOW, before the web server can accept any request. If the
  // server comes up first, an add/edit/delete (or a stray POST) lands while the
  // store is still empty and saveHosts() overwrites hosts.csv with an empty file —
  // wiping persisted hosts on the next boot.
  Csv::loadHosts(HOSTS_CSV_PATH);
  Store::recount();
  WifiPortal::begin();
  WebServer::begin();

  g_displayOk = Display::begin();   // TCA9554 expander -> RGB -> GT911 -> LVGL
  if (!g_displayOk)
    Serial.println("[HostMonitor] FATAL: display init failed (check PSRAM=OPI 8MB).");

  if (g_displayOk) {
    UI::begin();
    for (int i = 0; i < 8; i++) { Display::loop(); delay(10); }  // paint first frames
  }

  // Final bring-up — run INLINE rather than in a background task. The old "init"
  // task was created with a dynamic 16KB stack here, at the most heap-fragmented
  // point of boot; on reboots (hosts already loaded) that xTaskCreate could fail,
  // so Scheduler::begin() never ran and checks stayed "pending" / "sched not
  // started". Hosts are already loaded (above) and the remaining work is fast and
  // non-blocking, so doing it inline guarantees the check engine actually starts.
  g_bootStage = 1;
  Store::recount();
  Store::markDirty();
  g_bootStage = 4; Notifier::begin();    // SMTP + webhook init (no network, no flash)
  g_bootStage = 5; Scheduler::begin();   // creates the check task (static stack — can't fail)
  g_bootStage = 9;

  Serial.println("[HostMonitor] ready.");
}

void loop() {
  if (g_displayOk) {
    Display::loop();    // LVGL tick + timer handler
    UI::loop();         // refresh on-screen data when the store changes
  }
  WifiPortal::loop();   // captive DNS + reconnect (no-op until started)
  delay(2);
}
