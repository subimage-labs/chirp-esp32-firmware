#include "i2c_bus.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

static SemaphoreHandle_t busMutex() {
    static SemaphoreHandle_t m = xSemaphoreCreateRecursiveMutex();   // first use, any task
    return m;
}

I2cBusLock::I2cBusLock() { xSemaphoreTakeRecursive(busMutex(), portMAX_DELAY); }
I2cBusLock::~I2cBusLock() { xSemaphoreGiveRecursive(busMutex()); }
