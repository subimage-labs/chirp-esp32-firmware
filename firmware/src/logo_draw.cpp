#include "logo_draw.h"

#include "ui.h"

// One logical row expands to `scale` identical panel rows; one window and one
// pushPixels per logical row keep the SPI busy with pixel data only (the same
// as the character view's sprite, character.cpp).
void logoDraw(int x, int y, int scale) {
    scale = constrain(scale, 1, LOGO_MAX_SCALE);
    uint8_t frame[logo::PIXELS];
    logo::renderLogo(frame);
    uint16_t pal[logo::SlotCount];
    logo::paletteFor(uiInk(Ink::Bg), pal);
    for (int i = 0; i < logo::SlotCount; i++)
        pal[i] = (uint16_t)((pal[i] >> 8) | (pal[i] << 8));       // panel byte order

    const int fw = logo::W, fh = logo::H;
    const int pw = logoWidth(scale);
    alignas(4) static uint16_t rowbuf[LOGO_MAX_SCALE * logo::W * LOGO_MAX_SCALE + 2];   // +2: pushPixels reads whole words
    TFT_eSPI& tft = uiTft();
    tft.startWrite();
    tft.setAddrWindow(x, y, pw, logoHeight(scale));
    for (int row = 0; row < fh; row++) {
        const uint8_t* src = frame + row * fw;
        uint16_t* d = rowbuf;
        for (int col = 0; col < fw; col++)
            for (int k = 0; k < scale; k++) *d++ = pal[src[col]];
        for (int k = 1; k < scale; k++) memcpy(rowbuf + k * pw, rowbuf, pw * sizeof(uint16_t));
        tft.pushPixels(rowbuf, scale * pw);
    }
    tft.endWrite();
}

void logoSplash() {
    const int w = uiTft().width(), h = uiContentBottom();
    const int scale = logoScaleFor(w, h);
    logoDraw((w - logoWidth(scale)) / 2, (h - logoHeight(scale)) / 2, scale);
}
