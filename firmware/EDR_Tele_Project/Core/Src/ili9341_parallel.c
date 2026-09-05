// NOTE: Despite the filename, this driver is configured for an ILI9488
// panel (confirmed from board silkscreen), not the ILI9341. The ILI9488
// requires its own power/VCOM/gamma register sequence in TFT_Init() below;
// reusing the bare-minimum ILI9341 init sequence leaves the panel's analog
// drive undefined and the screen reads back as solid black.
#include "ili9341_parallel.h"

const uint8_t Font5x7[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, // Space (0x20)
    0x00, 0x00, 0x5F, 0x00, 0x00, // !
    0x00, 0x07, 0x00, 0x07, 0x00, // "
    0x14, 0x7F, 0x14, 0x7F, 0x14, // #
    0x24, 0x2A, 0x7F, 0x2A, 0x12, // $
    0x23, 0x13, 0x08, 0x64, 0x62, // %
    0x36, 0x49, 0x55, 0x22, 0x50, // &
    0x00, 0x05, 0x03, 0x00, 0x00, // '
    0x00, 0x1C, 0x22, 0x41, 0x00, // (
    0x00, 0x41, 0x22, 0x1C, 0x00, // )
    0x08, 0x2A, 0x1C, 0x2A, 0x08, // *
    0x08, 0x08, 0x3E, 0x08, 0x08, // +
    0x00, 0x50, 0x30, 0x00, 0x00, // ,
    0x08, 0x08, 0x08, 0x08, 0x08, // -
    0x00, 0x60, 0x60, 0x00, 0x00, // .
    0x20, 0x10, 0x08, 0x04, 0x02, // /
    0x3E, 0x51, 0x4F, 0x45, 0x3E, // 0
    0x00, 0x42, 0x7F, 0x40, 0x00, // 1
    0x42, 0x61, 0x51, 0x49, 0x46, // 2
    0x21, 0x41, 0x45, 0x4B, 0x31, // 3
    0x18, 0x14, 0x12, 0x7F, 0x10, // 4
    0x27, 0x45, 0x45, 0x45, 0x39, // 5
    0x3C, 0x4A, 0x49, 0x49, 0x30, // 6
    0x01, 0x71, 0x09, 0x05, 0x03, // 7
    0x36, 0x49, 0x49, 0x49, 0x36, // 8
    0x06, 0x49, 0x49, 0x29, 0x1E, // 9
    0x00, 0x36, 0x36, 0x00, 0x00, // :
    0x00, 0x56, 0x36, 0x00, 0x00, // ;
    0x00, 0x08, 0x14, 0x22, 0x41, // <
    0x14, 0x14, 0x14, 0x14, 0x14, // =
    0x41, 0x22, 0x14, 0x08, 0x00, // >
    0x02, 0x01, 0x51, 0x09, 0x06, // ?
    0x32, 0x49, 0x79, 0x41, 0x3E, // @
    0x7E, 0x11, 0x11, 0x11, 0x7E, // A
    0x7F, 0x49, 0x49, 0x49, 0x36, // B
    0x3E, 0x41, 0x41, 0x41, 0x22, // C
    0x7F, 0x41, 0x41, 0x22, 0x1C, // D
    0x7F, 0x49, 0x49, 0x49, 0x41, // E
    0x7F, 0x09, 0x09, 0x01, 0x01, // F
    0x3E, 0x41, 0x49, 0x49, 0x7A, // G
    0x7F, 0x08, 0x08, 0x08, 0x7F, // H
    0x00, 0x41, 0x7F, 0x41, 0x00, // I
    0x20, 0x40, 0x41, 0x3F, 0x01, // J
    0x7F, 0x08, 0x14, 0x22, 0x41, // K
    0x7F, 0x40, 0x40, 0x40, 0x40, // L
    0x7F, 0x02, 0x0C, 0x02, 0x7F, // M
    0x7F, 0x04, 0x08, 0x10, 0x7F, // N
    0x3E, 0x41, 0x41, 0x41, 0x3E, // O
    0x7F, 0x09, 0x09, 0x09, 0x06, // P
    0x3E, 0x41, 0x51, 0x21, 0x5E, // Q
    0x7F, 0x09, 0x19, 0x29, 0x46, // R
    0x46, 0x49, 0x49, 0x49, 0x31, // S
    0x01, 0x01, 0x7F, 0x01, 0x01, // T
    0x3F, 0x40, 0x40, 0x40, 0x3F, // U
    0x1F, 0x20, 0x40, 0x20, 0x1F, // V
    0x3F, 0x40, 0x38, 0x40, 0x3F, // W
    0x63, 0x14, 0x08, 0x14, 0x63, // X
    0x07, 0x08, 0x70, 0x08, 0x07, // Y
    0x61, 0x51, 0x49, 0x45, 0x43  // Z
};

static inline void WR_Strobe(void) {
    GPIOB->BSRR = GPIO_BSRR_BR0;  // WR Low
    __NOP(); __NOP();
    GPIOB->BSRR = GPIO_BSRR_BS0;  // WR High
}

static inline void Write_Bus(uint8_t data) {
    // Read Port E, preserve the lower bits (like PE7 Reset), clear high byte, and write data
    uint32_t odr = GPIOE->ODR;
    odr &= 0x00FF;
    odr |= ((uint16_t)data << 8);
    GPIOE->ODR = odr;

    WR_Strobe();
}

static inline void Write_Cmd(uint8_t cmd) {
    GPIOB->BSRR = GPIO_BSRR_BR1; // RS Low
    Write_Bus(cmd);
}

static inline void Write_Data(uint8_t data) {
    GPIOB->BSRR = GPIO_BSRR_BS1; // RS High
    Write_Bus(data);
}

void TFT_Init(void) {
    GPIOE->BSRR = GPIO_BSRR_BR7; // RST Cycle Low
    HAL_Delay(20);
    GPIOE->BSRR = GPIO_BSRR_BS7; // RST High
    HAL_Delay(150);

    GPIOC->BSRR = GPIO_BSRR_BS5; // RD High (Idle)
    GPIOB->BSRR = GPIO_BSRR_BR2; // CS Low (Permanently Selected)

    Write_Cmd(0x01); // Software Reset
    HAL_Delay(150);

    // --- ILI9488-specific power / gamma / VCOM setup ---
    // The ILI9341 minimal init (reset, pixel format, MADCTL, sleep-out)
    // is NOT enough for the ILI9488: without these registers the panel's
    // analog drive (power, VCOM, gamma) stays at an undefined reset state
    // and the screen reads back as solid black even though it is correctly
    // receiving and executing every command.
    Write_Cmd(0xC0); // Power Control 1
    Write_Data(0x17);
    Write_Data(0x15);

    Write_Cmd(0xC1); // Power Control 2
    Write_Data(0x41);

    Write_Cmd(0xC5); // VCOM Control
    Write_Data(0x00);
    Write_Data(0x12);
    Write_Data(0x80);

    Write_Cmd(0x36); // Memory Access Control
    Write_Data(0x48); // BGR + your existing orientation bits

    Write_Cmd(0x3A); // Interface Pixel Format
    Write_Data(0x55); // 16 bits/pixel — required for 8080 parallel bus

    Write_Cmd(0xB0); // Interface Mode Control
    Write_Data(0x00);

    Write_Cmd(0xB1); // Frame Rate Control
    Write_Data(0xA0);

    Write_Cmd(0xB4); // Display Inversion Control
    Write_Data(0x02);

    Write_Cmd(0xB6); // Display Function Control
    Write_Data(0x02);
    Write_Data(0x02);
    Write_Data(0x3B);

    Write_Cmd(0xE0); // Positive Gamma Control
    Write_Data(0x00); Write_Data(0x03); Write_Data(0x09); Write_Data(0x08);
    Write_Data(0x16); Write_Data(0x0A); Write_Data(0x3F); Write_Data(0x78);
    Write_Data(0x4C); Write_Data(0x09); Write_Data(0x0A); Write_Data(0x08);
    Write_Data(0x16); Write_Data(0x1A); Write_Data(0x0F);

    Write_Cmd(0xE1); // Negative Gamma Control
    Write_Data(0x00); Write_Data(0x16); Write_Data(0x19); Write_Data(0x03);
    Write_Data(0x0F); Write_Data(0x05); Write_Data(0x32); Write_Data(0x45);
    Write_Data(0x46); Write_Data(0x04); Write_Data(0x0E); Write_Data(0x0D);
    Write_Data(0x35); Write_Data(0x37); Write_Data(0x0F);

    Write_Cmd(0xF7); // Adjust Control 3
    Write_Data(0xA9); Write_Data(0x51); Write_Data(0x2C); Write_Data(0x82);

    Write_Cmd(0x11); // Exit Sleep
    HAL_Delay(150);
    Write_Cmd(0x29); // Display ON
    HAL_Delay(50);
}

static void SetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    Write_Cmd(0x2A);
    Write_Data(x0 >> 8); Write_Data(x0 & 0xFF);
    Write_Data(x1 >> 8); Write_Data(x1 & 0xFF);
    Write_Cmd(0x2B);
    Write_Data(y0 >> 8); Write_Data(y0 & 0xFF);
    Write_Data(y1 >> 8); Write_Data(y1 & 0xFF);
    Write_Cmd(0x2C);
}

void TFT_FillScreen(uint16_t color) {
    SetWindow(0, 0, TFT_WIDTH - 1, TFT_HEIGHT - 1);
    for (uint32_t i = 0; i < (TFT_WIDTH * TFT_HEIGHT); i++) {
        Write_Data(color >> 8);
        Write_Data(color & 0xFF);
    }
}

void TFT_DrawChar(uint16_t x, uint16_t y, char c, uint16_t color, uint16_t bg, uint8_t size) {
    // FIX: Expand boundary to 90 to allow 'G' through 'Z' (Space is 32)
    if (c < 32 || c > 90) return;

    // FIX: Changed to uint16_t to avoid potential 8-bit overflow during multiplication
    uint16_t font_index = c - 32;

    for (uint8_t i = 0; i < 5; i++) {
        uint8_t line = Font5x7[(font_index * 5) + i];
        for (uint8_t j = 0; j < 7; j++) {
            uint16_t p_color = (line & (1 << j)) ? color : bg;

            // Set boundary window
            SetWindow(x + (i * size), y + (j * size), x + (i * size) + size - 1, y + (j * size) + size - 1);

            // Send pixel data block over parallel bus
            for (uint32_t p = 0; p < (size * size); p++) {
                Write_Data(p_color >> 8);
                Write_Data(p_color & 0xFF);
            }
        }
    }
}

void TFT_DrawString(uint16_t x, uint16_t y, const char* str, uint16_t color, uint16_t bg, uint8_t size) {
    if (size == 0) size = 1;
    uint8_t char_width = 6 * size; // 5 pixels + 1 space pixel scaled

    while (*str) {
        if (x + char_width > TFT_WIDTH) {
            x = 0;
            y += 8 * size; // Advance down row bounds
        }
        TFT_DrawChar(x, y, *str++, color, bg, size);
        x += char_width;
    }
}
