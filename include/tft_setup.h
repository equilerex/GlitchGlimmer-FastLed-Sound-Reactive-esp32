// Display configuration for TFT_eSPI.
//
// TFT_eSPI normally reads its settings from its own User_Setup_Select.h, which
// is a file inside the library rather than anything this repo can carry, so a
// fresh clone would build against whatever display that copy happened to name.
// TFT_eSPI.h checks __has_include(<tft_setup.h>) before it includes that file,
// and PlatformIO puts this directory on the include path, so this header wins
// and the settings below are the whole story. It also sets USER_SETUP_LOADED,
// which stops User_Setup_Select.h from loading a competing setup.
//
// These values describe the project's 240x135 ST7789V panel on the ESP32-S3
// carrier board and are kept in-repo so the build does not depend on a local
// TFT_eSPI User_Setup_Select.h.

#define ST7789_DRIVER
#define TFT_SDA_READ        // this panel has a bidirectional SDA pin

#define TFT_WIDTH  240
#define TFT_HEIGHT 135

#define CGRAM_OFFSET        // the library adds the offsets this panel needs

#define TFT_MOSI 19
#define TFT_SCLK 18
#define TFT_CS    5
#define TFT_DC   16
#define TFT_RST  23

#define TFT_BL   4          // backlight control
#define TFT_BACKLIGHT_ON HIGH

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF

#define SMOOTH_FONT

#define SPI_FREQUENCY       40000000
#define SPI_READ_FREQUENCY   6000000   // 6 MHz is the ST7789V's read ceiling
