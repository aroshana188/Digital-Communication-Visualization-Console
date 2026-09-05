/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Typing UI - Accumulating message buffer with digits,
  *                    backspace, and A-E letters, using the proven
  *                    active-high row/no-pull column keypad scan logic.
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include <string.h>
#include <stdio.h>

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MPU_Config(void);
static void MX_GPIO_Init(void);

/* USER CODE BEGIN PFP */
/**
  * Generic parallel-bus pin description, so the same driver code can drive
  * either physical display just by passing a different LCD_Pins instance —
  * avoids duplicating every drawing function for Display 2.
  */
typedef struct {
  GPIO_TypeDef* rst_port; uint16_t rst_pin;
  GPIO_TypeDef* cs_port;  uint16_t cs_pin;
  GPIO_TypeDef* rs_port;  uint16_t rs_pin;  // RS/DC
  GPIO_TypeDef* wr_port;  uint16_t wr_pin;
  GPIO_TypeDef* rd_port;  uint16_t rd_pin;
  GPIO_TypeDef* data_port; // 8-bit data bus on bits 8-15 of this port (D0-D7)
} LCD_Pins;

static const LCD_Pins LCD1_PINS = {
  .rst_port = GPIOE, .rst_pin = GPIO_PIN_7,
  .cs_port  = GPIOB, .cs_pin  = GPIO_PIN_2,
  .rs_port  = GPIOB, .rs_pin  = GPIO_PIN_1,
  .wr_port  = GPIOB, .wr_pin  = GPIO_PIN_0,
  .rd_port  = GPIOC, .rd_pin  = GPIO_PIN_5,
  .data_port = GPIOE
};

static const LCD_Pins LCD2_PINS = {
  .rst_port = GPIOC, .rst_pin = GPIO_PIN_6,
  .cs_port  = GPIOC, .cs_pin  = GPIO_PIN_7,
  .rs_port  = GPIOC, .rs_pin  = GPIO_PIN_8,
  .wr_port  = GPIOC, .wr_pin  = GPIO_PIN_9,
  .rd_port  = GPIOA, .rd_pin  = GPIO_PIN_8,
  .data_port = GPIOD
};

static inline void WR_STROBE(const LCD_Pins* lcd);
void write8(const LCD_Pins* lcd, uint8_t d);
void writeCommand(const LCD_Pins* lcd, uint8_t c);
void writeData(const LCD_Pins* lcd, uint8_t d);
void lcdReset(const LCD_Pins* lcd);
void lcdInit(const LCD_Pins* lcd);
void setAddr(const LCD_Pins* lcd, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);
void fillScreen(const LCD_Pins* lcd, uint16_t color);
void drawRectangle(const LCD_Pins* lcd, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void drawChar(const LCD_Pins* lcd, char ch, uint16_t x, uint16_t y, uint8_t scale, uint16_t color, uint16_t bg);
void printString(const LCD_Pins* lcd, const char* str, uint16_t x, uint16_t y, uint8_t scale, uint16_t color, uint16_t bg);
void checkNavKeys(void);
void scanMatrixKeypad(void);
void handleMatrixKeyPress(uint8_t row, uint8_t col);
void Process_UI_Screen_Updates(void);
void Append_Char(char c);
void Backspace_Char(void);
void Confirm_Message(void);
void Move_Selection(int8_t delta);
void Confirm_Coding_Selection(void);
void Prob_Digit(uint8_t digit);
void Prob_Backspace(void);
void Confirm_Prob_Entry(void);
/* USER CODE END PFP */

typedef enum {
  STATE_ENTER_MSG,
  STATE_SELECT_CODING,
  STATE_ENTER_PROBS,    // Huffman/Shannon Fano: enter A-E probabilities
  STATE_FIXED_RESULT,   // Fixed Length: show A-E code table
  STATE_CONFIRMED       // Final confirmation screen
} AppState;

static AppState app_state = STATE_ENTER_MSG;
static AppState last_rendered_state = (AppState)-1; // forces first-time full draw
static int8_t coding_selection = 0; // 0=Fixed, 1=Huffman, 2=Shannon Fano
static int8_t last_rendered_selection = -1;

// Probability entry state
static uint8_t  prob_cursor = 0;           // which character (0=A .. 4=E) is active
static uint16_t prob_values[5] = {0,0,0,0,0}; // confirmed % for each character
static uint16_t prob_current = 0;          // digits being typed for the active char
static uint8_t  prob_has_input = 0;        // whether user typed anything yet
static uint8_t  last_prob_cursor = 0xFF;   // track cursor for partial redraws

// ---- CODING RESULT STATE ----
// Binary tree node (max 9 nodes for 5 symbols: 2*5-1)
#define MAX_NODES 9
typedef struct {
  int8_t  left;       // index of left child (-1 = none)
  int8_t  right;      // index of right child (-1 = none)
  int8_t  parent;     // index of parent (-1 = root)
  uint8_t is_leaf;    // 1 if this node is a symbol
  uint8_t symbol;     // 0-4 (A-E) if is_leaf
  uint16_t prob;      // combined probability (sum of children)
  uint16_t node_x;    // pixel x for drawing
  uint16_t node_y;    // pixel y for drawing
} TreeNode;

static TreeNode tree_nodes[MAX_NODES];
static uint8_t  tree_node_count = 0;
static int8_t   tree_root = -1;

// Generated codes for each symbol (A=0..E=4)
static char     sym_codes[5][8];   // e.g. "110\0"
static uint8_t  sym_code_len[5];

// Currently selected symbol on the result screen (for path highlighting)
static int8_t   result_cursor = 0;
static int8_t   last_result_cursor = -1;

void Build_Huffman_Tree(void);
void Build_Shannon_Fano(int8_t* indices, uint8_t count, uint8_t depth, uint8_t node_idx);
int8_t  SF_Make_Node(uint8_t is_leaf, uint8_t symbol, uint16_t prob);
void Layout_Tree(int8_t node, uint16_t x, uint16_t y_min, uint16_t y_max);
void Draw_Tree_On_D2(int8_t highlight_symbol);
void Draw_Result_Table(void);
void Extract_Codes(int8_t node, char* prefix, uint8_t depth);

static const char* coding_options[3] = {
  "Fixed Length Coding",
  "Huffman Coding",
  "Shannon Fano Coding"
};

/* Short descriptions shown on Display 2 while a coding method is selected,
   updating live as the user moves Up/Down before pressing OK. */
static const char* coding_descriptions[3] = {
  "FIXED LENGTH CODING ASSIGNS EVERY SYMBOL THE SAME NUMBER OF BITS, REGARDLESS OF HOW OFTEN IT APPEARS.",
  "HUFFMAN CODING BUILDS A BINARY TREE FROM SYMBOL FREQUENCIES, GIVING SHORTER CODES TO MORE FREQUENT SYMBOLS.",
  "SHANNON FANO CODING SPLITS SYMBOLS INTO TWO GROUPS OF NEARLY EQUAL PROBABILITY, REPEATEDLY, TO BUILD VARIABLE LENGTH CODES."
};

/* Industry Standard 5x7 Font Table for Alpha-Numerics */
const uint8_t standard_font5x7[][5] = {
  {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 0
  {0x00, 0x42, 0x7F, 0x40, 0x00}, // 1
  {0x42, 0x61, 0x51, 0x49, 0x46}, // 2
  {0x21, 0x41, 0x45, 0x4B, 0x31}, // 3
  {0x18, 0x14, 0x12, 0x7F, 0x10}, // 4
  {0x27, 0x45, 0x45, 0x45, 0x39}, // 5
  {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 6
  {0x01, 0x71, 0x09, 0x05, 0x03}, // 7
  {0x36, 0x49, 0x49, 0x49, 0x36}, // 8
  {0x06, 0x49, 0x49, 0x29, 0x1E}, // 9
  {0x7E, 0x11, 0x11, 0x11, 0x7E}, // A
  {0x7F, 0x49, 0x49, 0x49, 0x36}, // B
  {0x3E, 0x41, 0x41, 0x41, 0x22}, // C
  {0x7F, 0x41, 0x41, 0x22, 0x1C}, // D
  {0x7F, 0x49, 0x49, 0x49, 0x41}, // E
  {0x7F, 0x09, 0x09, 0x09, 0x01}, // F
  {0x3E, 0x41, 0x49, 0x49, 0x7A}, // G
  {0x7F, 0x08, 0x08, 0x08, 0x7F}, // H
  {0x00, 0x41, 0x7F, 0x41, 0x00}, // I
  {0x20, 0x40, 0x41, 0x3F, 0x01}, // J
  {0x7F, 0x08, 0x14, 0x22, 0x41}, // K
  {0x7F, 0x40, 0x40, 0x40, 0x40}, // L
  {0x7F, 0x02, 0x0C, 0x02, 0x7F}, // M
  {0x7F, 0x04, 0x08, 0x10, 0x7F}, // N
  {0x3E, 0x41, 0x41, 0x41, 0x3E}, // O
  {0x7F, 0x09, 0x09, 0x09, 0x06}, // P
  {0x3E, 0x41, 0x51, 0x21, 0x5E}, // Q
  {0x7F, 0x09, 0x19, 0x29, 0x46}, // R
  {0x46, 0x49, 0x49, 0x49, 0x31}, // S
  {0x01, 0x01, 0x7F, 0x01, 0x01}, // T
  {0x3F, 0x40, 0x40, 0x40, 0x3F}, // U
  {0x1F, 0x20, 0x40, 0x20, 0x1F}, // V
  {0x3F, 0x40, 0x38, 0x40, 0x3F}, // W
  {0x63, 0x14, 0x08, 0x14, 0x63}, // X
  {0x07, 0x08, 0x70, 0x08, 0x07}, // Y
  {0x61, 0x51, 0x49, 0x45, 0x43}, // Z
  {0x00, 0x00, 0x00, 0x00, 0x00}, // Space
  {0x40, 0x40, 0x40, 0x40, 0x40}, // _ (Cursor)
  {0x00, 0x36, 0x36, 0x00, 0x00}, // : (idx 38)
  {0x00, 0x1C, 0x22, 0x41, 0x00}, // ( (idx 39)
  {0x00, 0x41, 0x22, 0x1C, 0x00}, // ) (idx 40)
  {0x20, 0x10, 0x08, 0x04, 0x02}, // / (idx 41)
  {0x00, 0x60, 0x60, 0x00, 0x00}, // . (idx 42)
  {0x41, 0x22, 0x14, 0x08, 0x00}, // > (idx 43)
  {0x23, 0x13, 0x08, 0x64, 0x62}  // % (idx 44)
};

/* User-confirmed working layout: */
const char matrix_mapping[4][4] = {
  {'E', 'D', 'C', 'B'},
  {'A', '<', '0', '9'},
  {'8', '7', '6', '5'},
  {'4', '3', '2', '1'}
};

/* --- SYSTEM STRING VARIABLES (accumulating message) --- */
char message_buffer[64] = "";
int message_len = 0;

const uint16_t prompt_x = 10;
const uint16_t prompt_y = 15;
const uint8_t  prompt_scale = 2;

const uint16_t msg_x = 10;
const uint16_t msg_y = 60;
const uint8_t  msg_scale = 3;

int main(void)
{
  MPU_Config();
  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();

  /* USER CODE BEGIN 2 */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_2, GPIO_PIN_SET);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_SET);
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_5, GPIO_PIN_SET);

  lcdInit(&LCD1_PINS);
  lcdInit(&LCD2_PINS);

  message_buffer[0] = '\0';
  message_len = 0;

  Process_UI_Screen_Updates();
  /* USER CODE END 2 */

  while (1)
  {
    /* USER CODE BEGIN 3 */
    checkNavKeys();
    scanMatrixKeypad();
    HAL_Delay(10);
    /* USER CODE END 3 */
  }
}

/**
  * @brief Appends a character to the message buffer and refreshes the screen.
  */
void Append_Char(char c)
{
  if (app_state != STATE_ENTER_MSG) return;
  if (message_len < 63) {
    message_buffer[message_len++] = c;
    message_buffer[message_len] = '\0';
    Process_UI_Screen_Updates();
  }
}

/**
  * @brief Removes the last character from the message buffer and refreshes the screen.
  */
void Backspace_Char(void)
{
  if (app_state != STATE_ENTER_MSG) return;
  if (message_len > 0) {
    message_buffer[--message_len] = '\0';
    Process_UI_Screen_Updates();
  }
}

/**
  * @brief OK pressed while entering the message: move to coding-method
  *        selection screen. Ignored if the message is empty.
  */
void Confirm_Message(void)
{
  if (app_state != STATE_ENTER_MSG) return;
  if (message_len == 0) return; // nothing typed yet, ignore OK

  app_state = STATE_SELECT_CODING;
  coding_selection = 0;
  Process_UI_Screen_Updates();
}

/**
  * @brief Up/Down on the coding-selection screen moves the highlighted option.
  */
void Move_Selection(int8_t delta)
{
  if (app_state != STATE_SELECT_CODING) return;

  int8_t next = coding_selection + delta;
  if (next < 0) next = 0;
  if (next > 2) next = 2;

  if (next != coding_selection) {
    coding_selection = next;
    Process_UI_Screen_Updates();
  }
}

/**
  * @brief OK pressed on the coding-selection screen.
  *        Fixed Length -> show code table.
  *        Huffman / Shannon Fano -> enter probability screen.
  */
void Confirm_Coding_Selection(void)
{
  if (app_state != STATE_SELECT_CODING) return;

  if (coding_selection == 0) {
    // Fixed Length Coding — skip probabilities, show code table
    app_state = STATE_FIXED_RESULT;
  } else {
    // Huffman (1) or Shannon Fano (2) — need probabilities
    prob_cursor = 0;
    prob_current = 0;
    prob_has_input = 0;
    last_prob_cursor = 0xFF;
    for (uint8_t i = 0; i < 5; i++) prob_values[i] = 0;
    app_state = STATE_ENTER_PROBS;
  }
  Process_UI_Screen_Updates();
}

/**
  * @brief A digit key was pressed during probability entry.
  *        Builds the current value up to 3 digits (max 100).
  */
void Prob_Digit(uint8_t digit)
{
  if (app_state != STATE_ENTER_PROBS) return;
  uint16_t next = prob_current * 10 + digit;
  if (next > 100) return;
  prob_current = next;
  prob_has_input = 1;
  Process_UI_Screen_Updates();
}

/**
  * @brief Backspace during probability entry: drops the last typed digit.
  */
void Prob_Backspace(void)
{
  if (app_state != STATE_ENTER_PROBS) return;
  prob_current = prob_current / 10;
  Process_UI_Screen_Updates();
}

/**
  * @brief OK pressed during probability entry: confirms the current
  *        character's value and advances the cursor. After E, validates
  *        that all five values sum to 100.
  */
void Confirm_Prob_Entry(void)
{
  if (app_state != STATE_ENTER_PROBS) return;

  prob_values[prob_cursor] = prob_current;
  prob_current = 0;
  prob_has_input = 0;

  if (prob_cursor < 4) {
    prob_cursor++;
    last_prob_cursor = 0xFF;
    Process_UI_Screen_Updates();
  } else {
    uint16_t total = 0;
    for (uint8_t i = 0; i < 5; i++) total += prob_values[i];

    if (total != 100) {
      // Invalid: reset cursor to A, redraw entry screen, show error on D2
      prob_cursor = 0;
      last_prob_cursor = 0xFF;
      last_rendered_state = (AppState)-1; // force full D1 redraw
      Process_UI_Screen_Updates();
      drawRectangle(&LCD2_PINS, 0, 80, 480, 200, 0x0000);
      printString(&LCD2_PINS, "INVALID PROBABILITY", 10, 90, 2, 0xF800, 0x0000);
      printString(&LCD2_PINS, "VALUES. TOTAL MUST", 10, 120, 2, 0xF800, 0x0000);
      printString(&LCD2_PINS, "EQUAL 100%.", 10, 150, 2, 0xF800, 0x0000);
      printString(&LCD2_PINS, "RE-ENTER FROM A.", 10, 180, 2, 0xF800, 0x0000);
    } else {
      result_cursor = 0;
      last_result_cursor = -1;
      app_state = STATE_CONFIRMED;
      Process_UI_Screen_Updates();
    }
  }
}

/**
  * @brief Translates a matrix key press into a buffer edit. Only active
  *        while typing the message: A-E append a letter, '<' backspaces.
  *        Digits and '0' (space) are ignored per spec — only A-E allowed.
  *        Once in the coding-selection state, matrix keys are ignored;
  *        selection is done with nav buttons only.
  */
void handleMatrixKeyPress(uint8_t row, uint8_t col)
{
  char key = matrix_mapping[row][col];

  if (app_state == STATE_ENTER_MSG) {
    if (key == '<') {
      Backspace_Char();
    } else if (key >= 'A' && key <= 'E') {
      Append_Char(key);
    }
    // digits ignored in message entry

  } else if (app_state == STATE_ENTER_PROBS) {
    if (key >= '1' && key <= '9') {
      Prob_Digit(key - '0');
    } else if (key == '0') {
      Prob_Digit(0);
    } else if (key == '<') {
      Prob_Backspace();
    }
    // A-E ignored in probability entry
  }
}

void checkNavKeys(void)
{
  static uint8_t prev_B5 = 1, prev_B6 = 1, prev_B7 = 1, prev_B8 = 1, prev_B9 = 1;

  uint8_t curr_B5 = HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_5); // OK/Enter
  uint8_t curr_B6 = HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_6); // Right (unused)
  uint8_t curr_B7 = HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_7); // Left (unused)
  uint8_t curr_B8 = HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_8); // Down
  uint8_t curr_B9 = HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_9); // Up

  if (prev_B9 == 1 && curr_B9 == 0) { // Up
    if (app_state == STATE_SELECT_CODING) {
      Move_Selection(-1);
    } else if (app_state == STATE_CONFIRMED) {
      if (result_cursor > 0) { result_cursor--; Process_UI_Screen_Updates(); }
    }
  }
  if (prev_B8 == 1 && curr_B8 == 0) { // Down
    if (app_state == STATE_SELECT_CODING) {
      Move_Selection(+1);
    } else if (app_state == STATE_CONFIRMED) {
      if (result_cursor < 4) { result_cursor++; Process_UI_Screen_Updates(); }
    }
  }

  if (prev_B5 == 1 && curr_B5 == 0) {
    if (app_state == STATE_ENTER_MSG) {
      Confirm_Message();
    } else if (app_state == STATE_SELECT_CODING) {
      Confirm_Coding_Selection();
    } else if (app_state == STATE_ENTER_PROBS) {
      Confirm_Prob_Entry();
    }
  }

  prev_B5 = curr_B5; prev_B6 = curr_B6; prev_B7 = curr_B7; prev_B8 = curr_B8; prev_B9 = curr_B9;
}

/**
  * @brief Matrix Keypad Scanner — proven active-high row / no-pull column
  *        logic, confirmed working on this hardware. Unchanged from the
  *        version that correctly registered key changes.
  */
void scanMatrixKeypad(void)
{
    // All rows LOW
    HAL_GPIO_WritePin(GPIOD,
        GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7,
        GPIO_PIN_RESET);

    for(int r = 0; r < 4; r++)
    {
        // Activate one row
        HAL_GPIO_WritePin(GPIOD, GPIO_PIN_4 << r, GPIO_PIN_SET);

        HAL_Delay(1);

        uint8_t d0 = HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_0);
        uint8_t d1 = HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_1);
        uint8_t d2 = HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_2);
        uint8_t d3 = HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_3);

        if(d0)
        {
            handleMatrixKeyPress(r,0);
            while(HAL_GPIO_ReadPin(GPIOD,GPIO_PIN_0));
        }

        if(d1)
        {
            handleMatrixKeyPress(r,1);
            while(HAL_GPIO_ReadPin(GPIOD,GPIO_PIN_1));
        }

        if(d2)
        {
            handleMatrixKeyPress(r,2);
            while(HAL_GPIO_ReadPin(GPIOD,GPIO_PIN_2));
        }

        if(d3)
        {
            handleMatrixKeyPress(r,3);
            while(HAL_GPIO_ReadPin(GPIOD,GPIO_PIN_3));
        }

        HAL_GPIO_WritePin(GPIOD, GPIO_PIN_4 << r, GPIO_PIN_RESET);
    }
}

/**
  * @brief Redraws only Display 2's description text for the currently
  *        highlighted coding method. Called on state entry and every time
  *        Up/Down changes the selection, so the description updates live
  *        before OK is pressed. Clears just the description's text block
  *        (not the whole Display 2 screen) to minimize flicker, the same
  *        principle used for Display 1's partial redraws.
  */
static void Redraw_Coding_Description(void)
{
  const uint16_t desc_y = 100;
  const uint16_t desc_h = 200; // generous block height for a wrapped 1-2 sentence description
  drawRectangle(&LCD2_PINS, prompt_x, desc_y, 480 - prompt_x, desc_h, 0x0000);
  printString(&LCD2_PINS, coding_options[coding_selection], prompt_x, prompt_y, 2, 0x07E0, 0x0000);
  printString(&LCD2_PINS, coding_descriptions[coding_selection], prompt_x, desc_y, 2, 0xFFFF, 0x0000);
}

/**
  * @brief Redraws only the message line region (used while typing). drawChar
  *        now paints background pixels in the same pass as the glyph, so
  *        there's no separate clear-then-draw flash — text is overwritten
  *        in place. The "> " marker column and one trailing blank cell are
  *        explicitly redrawn so a backspaced character's leftover pixels
  *        are always covered.
  */
static void Redraw_Message_Line(void)
{
  printString(&LCD1_PINS, message_buffer, msg_x, msg_y, msg_scale, 0xFFFF, 0x0000);
  // One extra blank cell past the current text end, to erase the
  // character that was just backspaced off (drawChar already paints its
  // own background, but only for characters printString actually visits).
  uint16_t end_x = msg_x + (uint16_t)strlen(message_buffer) * (6 * msg_scale);
  drawChar(&LCD1_PINS, ' ', end_x, msg_y, msg_scale, 0xFFFF, 0x0000);
}

/**
  * @brief Redraws only the coding-option list (used when the highlighted
  *        selection moves). drawChar paints its own background, so no
  *        separate clear pass is needed here either.
  */
static void Redraw_Coding_Options(void)
{
  uint16_t opt_y = 80;
  for (int8_t i = 0; i < 3; i++) {
    uint16_t color = (i == coding_selection) ? 0xFFE0 : 0xFFFF; // highlight selected in yellow
    // Always redraw the marker column so a previously-selected row's ">"
    // gets overwritten with a blank when selection moves away from it.
    printString(&LCD1_PINS, i == coding_selection ? ">" : " ", prompt_x, opt_y, 2, color, 0x0000);
    printString(&LCD1_PINS, coding_options[i], prompt_x + 20, opt_y, 2, color, 0x0000);
    opt_y += 40;
  }
}

/**
  * @brief Partial redraw of the probability entry rows on Display 1.
  *        Only updates the value column and cursor highlight, so there's
  *        no full-screen flash when the user types a digit.
  */
static void Redraw_Prob_Screen(void)
{
  static const char char_labels[5] = {'A','B','C','D','E'};
  char buf[16];
  uint16_t row_y = 60;

  for (uint8_t i = 0; i < 5; i++) {
    uint16_t label_color = (i == prob_cursor) ? 0xFFE0 : 0xFFFF; // yellow = active
    uint16_t val_color   = (i == prob_cursor) ? 0x07FF : 0xFFFF; // cyan for active value

    // Character label
    snprintf(buf, sizeof(buf), "%c:", char_labels[i]);
    printString(&LCD1_PINS, buf, 10, row_y, 2, label_color, 0x0000);

    // Value: if this is the active row show what is being typed, else show confirmed value
    if (i == prob_cursor) {
      snprintf(buf, sizeof(buf), "%3u%%   ", prob_current);
    } else {
      snprintf(buf, sizeof(buf), "%3u%%   ", prob_values[i]);
    }
    printString(&LCD1_PINS, buf, 60, row_y, 2, val_color, 0x0000);

    row_y += 42;
  }
}

// ============================================================
//  HUFFMAN TREE BUILDER
// ============================================================
void Build_Huffman_Tree(void)
{
  // Working array of active node indices, sorted by prob ascending
  int8_t active[MAX_NODES];
  uint8_t active_count = 5;

  // Create one leaf per symbol
  tree_node_count = 0;
  for (uint8_t i = 0; i < 5; i++) {
    tree_nodes[tree_node_count] = (TreeNode){-1,-1,-1,1,i,prob_values[i],0,0};
    active[i] = (int8_t)tree_node_count++;
  }

  // Sort active by prob ascending (simple insertion sort — only 5 items)
  for (uint8_t i = 1; i < active_count; i++) {
    int8_t key = active[i];
    int8_t j = (int8_t)i - 1;
    while (j >= 0 && tree_nodes[active[j]].prob > tree_nodes[key].prob) {
      active[j+1] = active[j]; j--;
    }
    active[j+1] = key;
  }

  // Iteratively combine two lowest-prob nodes
  while (active_count > 1) {
    int8_t l = active[0], r = active[1];
    uint16_t combined = tree_nodes[l].prob + tree_nodes[r].prob;
    int8_t parent_idx = (int8_t)tree_node_count;
    tree_nodes[parent_idx] = (TreeNode){l, r, -1, 0, 0xFF, combined, 0, 0};
    tree_nodes[l].parent = parent_idx;
    tree_nodes[r].parent = parent_idx;
    tree_node_count++;

    // Remove first two, insert new node in sorted position
    active_count -= 2;
    for (uint8_t k = 0; k < active_count; k++) active[k] = active[k+2];
    uint8_t pos = 0;
    while (pos < active_count && tree_nodes[active[pos]].prob <= combined) pos++;
    for (uint8_t k = active_count; k > pos; k--) active[k] = active[k-1];
    active[pos] = parent_idx;
    active_count++;
  }
  tree_root = active[0];
}

// ============================================================
//  SHANNON-FANO TREE BUILDER
// ============================================================
int8_t SF_Make_Node(uint8_t is_leaf, uint8_t symbol, uint16_t prob)
{
  int8_t idx = (int8_t)tree_node_count++;
  tree_nodes[idx] = (TreeNode){-1,-1,-1,is_leaf,symbol,prob,0,0};
  return idx;
}

// indices[] = sorted (desc prob) symbol indices for this subgroup
// node_idx  = the internal node index to attach children to
void Build_Shannon_Fano(int8_t* indices, uint8_t count, uint8_t depth, uint8_t node_idx)
{
  if (count == 1) {
    // This index is already a leaf — attach directly
    tree_nodes[node_idx].is_leaf = 1;
    tree_nodes[node_idx].symbol  = (uint8_t)indices[0];
    return;
  }

  // Split into two groups of nearly equal cumulative probability
  uint16_t total = 0;
  for (uint8_t i = 0; i < count; i++) total += prob_values[indices[i]];
  uint16_t half = total / 2, running = 0;
  uint8_t split = 1;
  for (uint8_t i = 0; i < count - 1; i++) {
    running += prob_values[indices[i]];
    uint16_t diff_before = (half > running) ? (half - running) : (running - half);
    uint16_t diff_after  = (half > running + prob_values[indices[i+1]])
                         ? (half - running - prob_values[indices[i+1]])
                         : (running + prob_values[indices[i+1]] - half);
    if (diff_after < diff_before) split = i + 2; else { split = i + 1; break; }
  }

  // Left child (bit 0)
  uint16_t lp = 0; for (uint8_t i = 0; i < split; i++) lp += prob_values[indices[i]];
  int8_t l_idx = SF_Make_Node(0, 0xFF, lp);
  tree_nodes[node_idx].left = l_idx;
  tree_nodes[l_idx].parent  = (int8_t)node_idx;
  Build_Shannon_Fano(indices, split, depth+1, (uint8_t)l_idx);

  // Right child (bit 1)
  uint16_t rp = 0; for (uint8_t i = split; i < count; i++) rp += prob_values[indices[i]];
  int8_t r_idx = SF_Make_Node(0, 0xFF, rp);
  tree_nodes[node_idx].right = r_idx;
  tree_nodes[r_idx].parent   = (int8_t)node_idx;
  Build_Shannon_Fano(indices + split, count - split, depth+1, (uint8_t)r_idx);
}

// ============================================================
//  CODE EXTRACTION  (depth-first traversal)
// ============================================================
void Extract_Codes(int8_t node, char* prefix, uint8_t depth)
{
  if (node < 0 || depth > 7) return;
  if (tree_nodes[node].is_leaf) {
    uint8_t sym = tree_nodes[node].symbol;
    if (sym < 5) {
      prefix[depth] = '\0';
      for (uint8_t i = 0; i < depth; i++) sym_codes[sym][i] = prefix[i];
      sym_codes[sym][depth] = '\0';
      sym_code_len[sym] = depth;
    }
    return;
  }
  prefix[depth] = '0';
  Extract_Codes(tree_nodes[node].left,  prefix, depth+1);
  prefix[depth] = '1';
  Extract_Codes(tree_nodes[node].right, prefix, depth+1);
}

// ============================================================
//  TREE LAYOUT  (horizontal: root left, leaves right)
//  Assigns pixel x,y to every node recursively.
//  x is proportional to depth; y is spread evenly across [y_min, y_max].
// ============================================================
void Layout_Tree(int8_t node, uint16_t x, uint16_t y_min, uint16_t y_max)
{
  if (node < 0) return;
  tree_nodes[node].node_x = x;
  tree_nodes[node].node_y = (y_min + y_max) / 2;

  if (!tree_nodes[node].is_leaf) {
    uint16_t x_next = x + 90; // horizontal step per level
    uint16_t y_mid  = (y_min + y_max) / 2;
    Layout_Tree(tree_nodes[node].left,  x_next, y_min, y_mid);
    Layout_Tree(tree_nodes[node].right, x_next, y_mid, y_max);
  }
}

// ============================================================
//  DRAW A SINGLE TREE EDGE + NODE CIRCLE
// ============================================================
static void Draw_Tree_Edge(int8_t parent, int8_t child, uint8_t bit, uint16_t color)
{
  if (parent < 0 || child < 0) return;
  uint16_t x1 = tree_nodes[parent].node_x;
  uint16_t y1 = tree_nodes[parent].node_y;
  uint16_t x2 = tree_nodes[child].node_x;
  uint16_t y2 = tree_nodes[child].node_y;

  // Draw line as a series of small rectangles (1px wide)
  int16_t dx = (int16_t)x2 - x1, dy = (int16_t)y2 - y1;
  uint8_t steps = (uint8_t)(dx > 0 ? dx : -dx);
  if ((uint8_t)(dy > 0 ? dy : -dy) > steps) steps = (uint8_t)(dy > 0 ? dy : -dy);
  if (steps == 0) return;
  for (uint8_t s = 0; s <= steps; s++) {
    uint16_t px = (uint16_t)(x1 + (int16_t)s * dx / steps);
    uint16_t py = (uint16_t)(y1 + (int16_t)s * dy / steps);
    drawRectangle(&LCD2_PINS, px, py, 2, 2, color);
  }

  // Draw 0/1 label midway along the edge
  uint16_t mx = (uint16_t)(x1 + dx/2 - 4);
  uint16_t my = (uint16_t)(y1 + dy/2 - 4);
  char label[2] = {(char)('0' + bit), '\0'};
  printString(&LCD2_PINS, label, mx, my, 1, color, 0x0000);
}

// ============================================================
//  FULL TREE DRAW ON DISPLAY 2
//  highlight_symbol: 0-4 to colour that path, 0xFF = none
// ============================================================
void Draw_Tree_On_D2(int8_t highlight_symbol)
{
  fillScreen(&LCD2_PINS, 0x0000);

  if (tree_root < 0) return;

  // Determine which nodes are on the highlighted path
  uint8_t on_path[MAX_NODES] = {0};
  if (highlight_symbol < 5) {
    // Walk from leaf up to root, marking path nodes
    // Find the leaf for this symbol
    int8_t leaf = -1;
    for (uint8_t n = 0; n < tree_node_count; n++) {
      if (tree_nodes[n].is_leaf && tree_nodes[n].symbol == (uint8_t)highlight_symbol) {
        leaf = (int8_t)n; break;
      }
    }
    int8_t cur = leaf;
    while (cur >= 0) { on_path[cur] = 1; cur = tree_nodes[cur].parent; }
  }

  // Draw all edges first (grey base, then highlight on top)
  for (uint8_t n = 0; n < tree_node_count; n++) {
    if (tree_nodes[n].left >= 0) {
      int8_t child = tree_nodes[n].left;
      uint16_t color = (on_path[n] && on_path[child]) ? 0x07E0 : 0x4208; // green or dark grey
      Draw_Tree_Edge((int8_t)n, child, 0, color);
    }
    if (tree_nodes[n].right >= 0) {
      int8_t child = tree_nodes[n].right;
      uint16_t color = (on_path[n] && on_path[child]) ? 0x07E0 : 0x4208;
      Draw_Tree_Edge((int8_t)n, child, 1, color);
    }
  }

  // Draw nodes on top of edges
  for (uint8_t n = 0; n < tree_node_count; n++) {
    uint16_t nx = tree_nodes[n].node_x;
    uint16_t ny = tree_nodes[n].node_y;
    uint16_t node_color = on_path[n] ? 0x07E0 : 0xAD55; // green or grey

    if (tree_nodes[n].is_leaf) {
      // Leaf: draw filled square + symbol label
      node_color = on_path[n] ? 0xFFE0 : 0x3186; // yellow highlight or blue-grey
      drawRectangle(&LCD2_PINS, nx-6, ny-6, 13, 13, node_color);
      char sym[3];
      sym[0] = 'A' + tree_nodes[n].symbol;
      sym[1] = '\0';
      printString(&LCD2_PINS, sym, nx-4, ny-5, 1, 0x0000, node_color);
    } else {
      // Internal node: small circle (drawn as filled square)
      drawRectangle(&LCD2_PINS, nx-4, ny-4, 9, 9, node_color);
    }
    char prob_buf[8];
    snprintf(prob_buf, sizeof(prob_buf), "%u/100", tree_nodes[n].prob);
    printString(&LCD2_PINS, prob_buf, nx-15, ny+10, 1, 0xFFFF, 0x0000);
  }
}

// ============================================================
//  DISPLAY 1: CODE TABLE WITH HIGHLIGHTED ROW
// ============================================================
void Draw_Result_Table(void)
{
  static const char labels[5] = {'A','B','C','D','E'};
  char buf[24];
  uint16_t row_y = 55;

  for (uint8_t i = 0; i < 5; i++) {
    uint16_t fg = (i == (uint8_t)result_cursor) ? 0x0000 : 0xFFFF;
    uint16_t bg = (i == (uint8_t)result_cursor) ? 0x07E0 : 0x0000; // green highlight
    snprintf(buf, sizeof(buf), "%c %3u%% %s", labels[i], prob_values[i], sym_codes[i]);
    // Pad to fixed width so background covers old longer codes cleanly
    printString(&LCD1_PINS, buf, 10, row_y, 2, fg, bg);
    // Fill rest of row with bg to avoid leftover pixels
    drawRectangle(&LCD1_PINS, 10 + (int16_t)strlen(buf)*12, row_y, 480-10-(int16_t)strlen(buf)*12, 18, bg);
    row_y += 46;
  }
}

/**
  * @brief Draws the static (unchanging) parts of each screen. Only called
  *        once per state transition — this is the only place that does a
  *        full-screen clear, which is what was causing the black flash on
  *        every single keypress before.
  */
static void Draw_Static_Screen(void)
{
  fillScreen(&LCD1_PINS, 0x0000);
  fillScreen(&LCD2_PINS, 0x0000);

  if (app_state == STATE_ENTER_MSG) {
    printString(&LCD1_PINS, "ENTER MSG (A-E):", prompt_x, prompt_y, prompt_scale, 0x07E0, 0x0000);
    printString(&LCD1_PINS, "PRESS OK WHEN DONE", prompt_x, 280, 2, 0xFFE0, 0x0000);

    printString(&LCD2_PINS, "ENTER THE MESSAGE TO BE", prompt_x, prompt_y, 2, 0x07E0, 0x0000);
    printString(&LCD2_PINS, "TRANSMITTED.", prompt_x, prompt_y + 20, 2, 0x07E0, 0x0000);

  } else if (app_state == STATE_SELECT_CODING) {
    printString(&LCD1_PINS, "SELECT SOURCE", prompt_x, prompt_y, prompt_scale, 0x07E0, 0x0000);
    printString(&LCD1_PINS, "CODING METHOD:", prompt_x, prompt_y + 20, prompt_scale, 0x07E0, 0x0000);
    printString(&LCD1_PINS, "UP/DOWN: CHOOSE  OK: CONFIRM", prompt_x, 280, 1, 0x07E0, 0x0000);
    // Display 2 description drawn by Process_UI_Screen_Updates via Redraw_Coding_Description

  } else if (app_state == STATE_ENTER_PROBS) {
    printString(&LCD1_PINS, "ENTER THE", prompt_x, prompt_y, 2, 0x07E0, 0x0000);
    printString(&LCD1_PINS, "PROBABILITIES:", prompt_x, prompt_y + 20, 2, 0x07E0, 0x0000);
    printString(&LCD1_PINS, "TYPE % THEN OK", prompt_x, 278, 1, 0xFFE0, 0x0000);

    printString(&LCD2_PINS, "ENTER PROBABILITIES AS", prompt_x, prompt_y, 2, 0x07E0, 0x0000);
    printString(&LCD2_PINS, "PERCENTAGES.", prompt_x, prompt_y + 20, 2, 0x07E0, 0x0000);
    printString(&LCD2_PINS, "ALL 5 VALUES MUST SUM", prompt_x, 80, 2, 0xFFFF, 0x0000);
    printString(&LCD2_PINS, "TO 100%.", prompt_x, 110, 2, 0xFFFF, 0x0000);

  } else if (app_state == STATE_FIXED_RESULT) {
    // Fixed Length code table: A-E -> 3-bit codes
    printString(&LCD1_PINS, "FIXED LENGTH CODES:", prompt_x, prompt_y, 2, 0x07E0, 0x0000);
    static const char* fixed_codes[5] = {"A: 000","B: 001","C: 011","D: 010","E: 110"};
    uint16_t fy = 60;
    for (uint8_t i = 0; i < 5; i++) {
      printString(&LCD1_PINS, fixed_codes[i], 40, fy, 3, 0xFFFF, 0x0000);
      fy += 48;
    }
    printString(&LCD2_PINS, "FIXED LENGTH CODING", prompt_x, prompt_y, 2, 0x07E0, 0x0000);
    printString(&LCD2_PINS, "ASSIGNS EQUAL BIT", prompt_x, 50, 2, 0xFFFF, 0x0000);
    printString(&LCD2_PINS, "LENGTH TO EACH SYMBOL.", prompt_x, 80, 2, 0xFFFF, 0x0000);

  } else if (app_state == STATE_CONFIRMED) {
    // Build tree, extract codes, layout for drawing
    tree_node_count = 0; tree_root = -1;
    for (uint8_t i = 0; i < 5; i++) { sym_codes[i][0]='\0'; sym_code_len[i]=0; }

    if (coding_selection == 1) {
      // Huffman
      Build_Huffman_Tree();
    } else {
      // Shannon-Fano: sort symbols by prob descending
      int8_t sf_idx[5] = {0,1,2,3,4};
      for (uint8_t i = 1; i < 5; i++) {
        int8_t key = sf_idx[i]; int8_t j = (int8_t)i-1;
        while (j >= 0 && prob_values[sf_idx[j]] < prob_values[key]) { sf_idx[j+1]=sf_idx[j]; j--; }
        sf_idx[j+1] = key;
      }
      // Create root node, then recursively build tree
      tree_root = SF_Make_Node(0, 0xFF, 100);
      Build_Shannon_Fano(sf_idx, 5, 0, (uint8_t)tree_root);
    }

    char prefix_buf[8];
    Extract_Codes(tree_root, prefix_buf, 0);

    // Layout: root at x=20, spread y across 30..290 (leaves need room)
    Layout_Tree(tree_root, 20, 10, 310);

    // Draw Display 1 header
    printString(&LCD1_PINS, coding_selection==1 ? "HUFFMAN CODING" : "SHANNON FANO", 10, prompt_y, 2, 0x07E0, 0x0000);
    printString(&LCD1_PINS, "UP/DOWN: SELECT CHAR", 10, 278, 1, 0xFFE0, 0x0000);

    // Draw Display 2 tree (no highlight yet — will be drawn in Process_UI)
    // (handled by partial-update path below)
  }
}

/**
  * @brief Top-level redraw dispatcher. Does a full-screen redraw only when
  *        the app state actually changed; otherwise only repaints the
  *        specific region that changed (message text, selection highlight,
  *        or probability row), avoiding the full-screen black flash.
  */
void Process_UI_Screen_Updates(void)
{
  uint8_t state_changed = (app_state != last_rendered_state);

  if (state_changed) {
    Draw_Static_Screen();
    last_rendered_state = app_state;
    last_rendered_selection = -1;
    last_prob_cursor = 0xFF;
  }

  if (app_state == STATE_ENTER_MSG) {
    Redraw_Message_Line();

  } else if (app_state == STATE_SELECT_CODING) {
    if (state_changed || coding_selection != last_rendered_selection) {
      Redraw_Coding_Options();
      Redraw_Coding_Description();
      last_rendered_selection = coding_selection;
    }

  } else if (app_state == STATE_ENTER_PROBS) {
    // Redraw the full character list — each row is small and fast;
    // doing all 5 avoids tracking individual-row dirty state.
    Redraw_Prob_Screen();
    last_prob_cursor = prob_cursor;

  } else if (app_state == STATE_CONFIRMED) {
    // Redraw D1 code table and D2 highlighted tree path whenever cursor moves
    if (state_changed || result_cursor != last_result_cursor) {
      Draw_Result_Table();
      Draw_Tree_On_D2(result_cursor);
      last_result_cursor = result_cursor;
    }
  }
  // STATE_FIXED_RESULT: nothing to update after static draw.
}

/* USER CODE BEGIN 4 */
void drawChar(const LCD_Pins* lcd, char ch, uint16_t x, uint16_t y, uint8_t scale, uint16_t color, uint16_t bg)
{
  uint8_t idx = 36;
  if (ch >= 'a' && ch <= 'z') ch = ch - 'a' + 'A'; // fold lowercase to uppercase glyphs

  if (ch >= '0' && ch <= '9') idx = ch - '0';
  else if (ch >= 'A' && ch <= 'Z') idx = (ch - 'A') + 10;
  else if (ch == '_') idx = 37;
  else if (ch == ' ') idx = 36;
  else if (ch == ':') idx = 38;
  else if (ch == '(') idx = 39;
  else if (ch == ')') idx = 40;
  else if (ch == '/') idx = 41;
  else if (ch == '.') idx = 42;
  else if (ch == '>') idx = 43;
  else if (ch == '%') idx = 44;

  // Draw the full 5x8 cell in one pass — every pixel gets either the
  // foreground or background color, so the caller doesn't need to clear
  // the area first. That separate clear-then-draw was the source of the
  // remaining flicker: a visible black gap between the clear and the text
  // appearing.
  for (uint8_t col = 0; col < 5; col++) {
    uint8_t line = standard_font5x7[idx][col];
    for (uint8_t row = 0; row < 8; row++) {
      uint16_t pixel_color = (line & 0x01) ? color : bg;
      drawRectangle(lcd, x + (col * scale), y + (row * scale), scale, scale, pixel_color);
      line >>= 1;
    }
  }
  // Column gap between characters also needs to be painted with bg,
  // otherwise leftover pixels from a previous longer string remain there.
  drawRectangle(lcd, x + (5 * scale), y, scale, 8 * scale, bg);
}

void printString(const LCD_Pins* lcd, const char* str, uint16_t x, uint16_t y, uint8_t scale, uint16_t color, uint16_t bg)
{
  uint16_t start_x = x;
  uint16_t char_width = 6 * scale;
  while (*str) {
    if (x + char_width > 480) {
      x = start_x;
      y += 8 * scale;
    }
    drawChar(lcd, *str, x, y, scale, color, bg);
    x += char_width;
    str++;
  }
}

void drawRectangle(const LCD_Pins* lcd, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
  setAddr(lcd, x, y, x + w - 1, y + h - 1);
  HAL_GPIO_WritePin(lcd->rs_port, lcd->rs_pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(lcd->cs_port, lcd->cs_pin, GPIO_PIN_RESET);

  uint8_t hi = color >> 8; uint8_t lo = color & 0xFF;
  uint32_t hi_bsrr = 0xFF000000 | ((uint32_t)hi << 8);
  uint32_t lo_bsrr = 0xFF000000 | ((uint32_t)lo << 8);
  uint32_t wr_set_bsrr = lcd->wr_pin;
  uint32_t wr_reset_bsrr = (uint32_t)lcd->wr_pin << 16;

  uint32_t total_pixels = (uint32_t)w * h;
  for(uint32_t i = 0; i < total_pixels; i++)
  {
      lcd->data_port->BSRR = hi_bsrr; lcd->wr_port->BSRR = wr_reset_bsrr; __asm__ volatile("nop"); lcd->wr_port->BSRR = wr_set_bsrr;
      lcd->data_port->BSRR = lo_bsrr; lcd->wr_port->BSRR = wr_reset_bsrr; __asm__ volatile("nop"); lcd->wr_port->BSRR = wr_set_bsrr;
  }
  HAL_GPIO_WritePin(lcd->cs_port, lcd->cs_pin, GPIO_PIN_SET);
}

static inline void WR_STROBE(const LCD_Pins* lcd) {
  lcd->wr_port->BSRR = (uint32_t)lcd->wr_pin << 16;
  __asm__ volatile("nop");
  lcd->wr_port->BSRR = lcd->wr_pin;
}
void write8(const LCD_Pins* lcd, uint8_t d) { lcd->data_port->BSRR = 0xFF000000 | ((uint32_t)d << 8); WR_STROBE(lcd); }
void writeCommand(const LCD_Pins* lcd, uint8_t c) { HAL_GPIO_WritePin(lcd->rs_port, lcd->rs_pin, GPIO_PIN_RESET); HAL_GPIO_WritePin(lcd->cs_port, lcd->cs_pin, GPIO_PIN_RESET); write8(lcd, c); HAL_GPIO_WritePin(lcd->cs_port, lcd->cs_pin, GPIO_PIN_SET); }
void writeData(const LCD_Pins* lcd, uint8_t d) { HAL_GPIO_WritePin(lcd->rs_port, lcd->rs_pin, GPIO_PIN_SET); HAL_GPIO_WritePin(lcd->cs_port, lcd->cs_pin, GPIO_PIN_RESET); write8(lcd, d); HAL_GPIO_WritePin(lcd->cs_port, lcd->cs_pin, GPIO_PIN_SET); }
void lcdReset(const LCD_Pins* lcd) { HAL_GPIO_WritePin(lcd->rst_port, lcd->rst_pin, GPIO_PIN_RESET); HAL_Delay(20); HAL_GPIO_WritePin(lcd->rst_port, lcd->rst_pin, GPIO_PIN_SET); HAL_Delay(150); }
void lcdInit(const LCD_Pins* lcd) { HAL_GPIO_WritePin(lcd->rd_port, lcd->rd_pin, GPIO_PIN_SET); lcdReset(lcd); writeCommand(lcd, 0x01); HAL_Delay(120); writeCommand(lcd, 0x11); HAL_Delay(120); writeCommand(lcd, 0x3A); writeData(lcd, 0x55); writeCommand(lcd, 0x36); writeData(lcd, 0x28); writeCommand(lcd, 0x29); HAL_Delay(20); }
void setAddr(const LCD_Pins* lcd, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) { writeCommand(lcd, 0x2A); writeData(lcd, x1 >> 8); writeData(lcd, x1); writeData(lcd, x2 >> 8); writeData(lcd, x2); writeCommand(lcd, 0x2B); writeData(lcd, y1 >> 8); writeData(lcd, y1); writeData(lcd, y2 >> 8); writeData(lcd, y2); writeCommand(lcd, 0x2C); }
void fillScreen(const LCD_Pins* lcd, uint16_t color) { drawRectangle(lcd, 0, 0, 480, 320, color); }
/* USER CODE END 4 */

void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);
  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) { Error_Handler(); }
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK|RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2|RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV1;
  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK) { Error_Handler(); }
}

static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_5, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_2, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOE, GPIO_PIN_7|GPIO_PIN_8|GPIO_PIN_9|GPIO_PIN_10|GPIO_PIN_11|GPIO_PIN_12|GPIO_PIN_13|GPIO_PIN_14|GPIO_PIN_15, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6|GPIO_PIN_7, GPIO_PIN_SET);

  GPIO_InitStruct.Pin = GPIO_PIN_5;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_2;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = GPIO_PIN_5|GPIO_PIN_6|GPIO_PIN_7|GPIO_PIN_8|GPIO_PIN_9;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = GPIO_PIN_7|GPIO_PIN_8|GPIO_PIN_9|GPIO_PIN_10|GPIO_PIN_11|GPIO_PIN_12|GPIO_PIN_13|GPIO_PIN_14|GPIO_PIN_15;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  // MATRIX COLUMNS INPUTS (PD0-PD3)
  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_3;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  // MATRIX ROWS OUTPUTS (PD4-PD7)
  GPIO_InitStruct.Pin = GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6|GPIO_PIN_7;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  // ---- DISPLAY 2 CONFIGURATION ----
  // D2 RST=PC6, CS=PC7, RS=PC8, WR=PC9 — control outputs on GPIOC
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_6|GPIO_PIN_7|GPIO_PIN_8|GPIO_PIN_9, GPIO_PIN_RESET);
  GPIO_InitStruct.Pin = GPIO_PIN_6|GPIO_PIN_7|GPIO_PIN_8|GPIO_PIN_9;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  // D2 RD=PA8 — output, idle high
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8, GPIO_PIN_SET);
  GPIO_InitStruct.Pin = GPIO_PIN_8;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  // D2 data bus D0-D7 on PD8-PD15 (upper byte of Port D)
  // Note: PD0-PD7 already configured above for keypad; PD8-PD15 are independent.
  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_8|GPIO_PIN_9|GPIO_PIN_10|GPIO_PIN_11|GPIO_PIN_12|GPIO_PIN_13|GPIO_PIN_14|GPIO_PIN_15, GPIO_PIN_RESET);
  GPIO_InitStruct.Pin = GPIO_PIN_8|GPIO_PIN_9|GPIO_PIN_10|GPIO_PIN_11|GPIO_PIN_12|GPIO_PIN_13|GPIO_PIN_14|GPIO_PIN_15;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);
}

void MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct = {0};
  HAL_MPU_Disable();
  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.Number = MPU_REGION_NUMBER0;
  MPU_InitStruct.BaseAddress = 0x0;
  MPU_InitStruct.Size = MPU_REGION_SIZE_4GB;
  MPU_InitStruct.SubRegionDisable = 0x87;
  MPU_InitStruct.AccessPermission = MPU_REGION_NO_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  HAL_MPU_ConfigRegion(&MPU_InitStruct);
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

void Error_Handler(void) { __disable_irq(); while (1) {} }
