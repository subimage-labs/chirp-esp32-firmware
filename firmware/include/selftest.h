// selftest.h — boot reachability check: Hermes (with the key) and the speech
// server. Loop task, during setup() after the WiFi join. Logs one
// `[SelfTest] ok` or `[SelfTest] <problem>` line (host/pair.py waits for it;
// never hosts or keys) and returns the problem for the status strip, or
// nullptr when everything answers. Messages: selftest_policy.h.
#pragma once

const char* selfTestRun();
