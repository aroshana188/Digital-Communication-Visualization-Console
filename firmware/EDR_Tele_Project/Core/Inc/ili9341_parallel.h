#ifndef ILI9341_PARALLEL_H
#define ILI9341_PARALLEL_H

#include "stm32h7xx_hal.h"

// Color Definitions (RGB565)
#define WHITE        0xFFFF
#define BLACK        0x0000
#define RED          0xF800
#define GREEN        0x07E0
#define BLUE         0x001F

// Display Dimensions
#define TFT_WIDTH    320
#define TFT_HEIGHT   480

// Public Drivers (Make sure 'uint8_t size' is included at the end)
void TFT_Init(void);
void TFT_FillScreen(uint16_t color);
void TFT_DrawChar(uint16_t x, uint16_t y, char c, uint16_t color, uint16_t bg, uint8_t size);
void TFT_DrawString(uint16_t x, uint16_t y, const char* str, uint16_t color, uint16_t bg, uint8_t size);

#endif
