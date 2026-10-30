// console.h — the one USB-serial line reader. Loop task while the app runs,
// the portal loop in setup mode. Owns the partial input line.
//
//   settings {json}  provisioning (host/provision.py, host/pair.py). Members
//                    as in settingsInputFromJson (settings.h): absent ones
//                    keep the stored value; ssid + pass add a WiFi network
//                    (forgetWifi removes one); profile + key adds that agent.
//                    Same rules as the setup form (setup_form.h). Never
//                    echoed. Replies one line, "[Settings] saved, restarting"
//                    or "[Settings] rejected: <reason>"; restarts after a save.
//   anything else    bench commands (BENCH_CMDS builds, app only): bench.h.
#pragma once

void consolePoll(bool app);
