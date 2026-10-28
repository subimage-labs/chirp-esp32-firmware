// Setup for the LilyGo T-Deck (ESP32-S3, ST7789 240x320 glass, mounted
// landscape). Pins from Xinyuan-LilyGO/T-Deck examples/UnitTest/utilities.h;
// the panel init sequence both boards share lives in TFT_Drivers/ST7789_Init.h.

#define USER_SETUP_ID 215
#define ST7789_DRIVER     // Configure all registers

// The controller's native frame: rotation 1 (board_tdeck.h) turns it into
// the upright 320x240 the device shows, as LILYGO's own examples do.
#define TFT_WIDTH  240
#define TFT_HEIGHT 320

// The panel's own voltages and gamma (TFT_Drivers/ST7789_Init.h), and its
// colour order, as LILYGO's Setup210 has them: the default sequence tints
// this glass (a near-black theme background showed green).
#define INIT_SEQUENCE_2
#define TFT_RGB_ORDER TFT_RGB

// ESP32-S3 + TFT_eSPI direct register writes are unstable on the default FSPI
// path with newer Arduino-ESP32 packages; route the panel through HSPI
// instead (same reason as Setup214).
#define USE_HSPI_PORT

#define TFT_INVERSION_ON

#define TFT_BL     42   // LED back-light
#define TFT_MISO   38
#define TFT_MOSI   41
#define TFT_SCLK   40
#define TFT_CS     12
#define TFT_DC     11
#define TFT_RST    -1

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

#define SPI_FREQUENCY  40000000

#define SPI_READ_FREQUENCY  20000000

#define SPI_TOUCH_FREQUENCY  2500000
