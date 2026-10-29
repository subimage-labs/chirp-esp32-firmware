// i2c_bus.h — the board's shared I2C bus (Wire). Wire locks each
// transaction, not a request together with the reads that empty its
// receive buffer, so code that reads through Wire while another task may
// use it holds this lock across both (T-Deck: the keyboard's poll task and
// the mic codec's setup). Recursive. Any context.
#pragma once

class I2cBusLock {
public:
    I2cBusLock();
    ~I2cBusLock();
    I2cBusLock(const I2cBusLock&) = delete;
    I2cBusLock& operator=(const I2cBusLock&) = delete;
};
