// app_state.h — the app state machine values (pure, shared with host tests).
#pragma once

#include <stdint.h>

enum UiState : uint8_t { UI_BOOT, UI_IDLE, UI_RECORDING, UI_THINKING, UI_SPEAKING };
