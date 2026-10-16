

#define USER_SETUP_ID 214

#define ST7789_DRIVER     // Configure all registers

#define TFT_WIDTH  170
#define TFT_HEIGHT 320

//#define TFT_RGB_ORDER TFT_RGB  // Colour order Red-Green-Blue
//#define TFT_RGB_ORDER TFT_BGR  // Colour order Blue-Green-Red

// ESP32-S3 + TFT_eSPI direct register writes are unstable on the default FSPI path
// with newer Arduino-ESP32 packages; route the panel through HSPI instead.
#define USE_HSPI_PORT

#define TFT_INVERSION_ON
//#define TFT_INVERSION_OFF
// #define TFT_BACKLIGHT_ON 1

#define TFT_BL     21   // LED back-light
#define TFT_MISO   10   
#define TFT_MOSI   9
#define TFT_SCLK   11
#define TFT_CS     41 
#define TFT_DC     16
#define TFT_RST    -1 // Connect reset to ensure display initialises


// #define TFT_BL     21   // LED back-light
// #define TFT_MISO   10   
// #define TFT_MOSI   9
// #define TFT_SCLK   11
// #define TFT_CS     41 
// #define TFT_DC     16
// #define TFT_RST    -1 // Connect reset to ensure display initialises



// Text is drawn only with the anti-aliased fonts in firmware/src/fonts (see ui.h);
// the built-in bitmap fonts are not compiled in.
//#define LOAD_GLCD
//#define LOAD_FONT2
//#define LOAD_FONT4
//#define LOAD_FONT6
//#define LOAD_FONT7
//#define LOAD_FONT8
//#define LOAD_FONT8N
//#define LOAD_GFXFF

#define SMOOTH_FONT


// #define SPI_FREQUENCY  27000000
#define SPI_FREQUENCY  40000000

#define SPI_READ_FREQUENCY  20000000

#define SPI_TOUCH_FREQUENCY  2500000

// #define SUPPORT_TRANSACTIONS
