// portal.h — setup mode: a WPA2 soft AP (name, 12-character password and join
// QR code on the screen) serving a captive web form for the device settings.
// /save needs this run's form token and, when the browser names an Origin,
// the portal's own address.
//
// Setup mode owns the whole boot: setup() enters it before any app module or
// task starts, and it never returns. It restarts after a save; with usable
// settings already stored, also on a side-key tap, after PORTAL_IDLE_MS
// without a phone, or PORTAL_MAX_MS after it started. Entered when the stored
// settings are unusable, or from the menu (portalRestartInto sets an RTC flag
// that survives the restart).
#pragma once

bool portalRequested();              // reads and clears the menu's flag
[[noreturn]] void portalRestartInto();
[[noreturn]] void portalRun();
