#include <SPI.h>
#include <IntervalTimer.h>


// Pin definitions
const int latchPin = 9; // Connected to RCLK on SN74HC595

// Display characteristics
const int X_DIM = 4;
const int Y_DIM = 6;

// Data format constants
const int BIT_SEPARATOR = 2;
const int END_PADDING = 0;
const int DATA_LEN = 3;

// Data arrays
byte colors[X_DIM][Y_DIM][3]; // Pixel colors in RGB format
byte write_data[X_DIM][DATA_LEN]; // Processed data used for BAM

// BAM tracking variables
int bam_cycle = 0;
int row_cycle = 0;
const int cycle_delay = 400;
int cycle_delay_count = 0;

// Defining some standard colors
int NUM_COLORS = 7;
uint8_t color_values[][3] = { { 0xF, 0x0, 0x0 }, // Red
                              { 0xF, 0x3, 0x0 }, // Orange
                              { 0xF, 0x8, 0x0 }, // Yellow
                              { 0x0, 0xF, 0x0 }, // Green
                              { 0x0, 0x0, 0xF }, // Blue
                              { 0x8, 0x0, 0xF }, // Purple
                              { 0xF, 0x1, 0x3}}; // Pink

uint8_t red[] = { 0xF, 0x0, 0x0 };
uint8_t green[] = { 0x0, 0xF, 0x0 };
uint8_t blue[] = { 0x0, 0x0, 0xF };
uint8_t purple[] = { 0xF, 0x0, 0xF };
IntervalTimer myTimer;

// Timer interrupt handler to multiplex and perform BAM
// 1 of 4 rows is lit during each execution
// Individual LEDs/colors are ON/OFF based on their BAM cycle
void timerCallback() {
  if (bam_cycle == 0) {
      format_all(0);
      updateShiftRegister(write_data[row_cycle]);
    } else if (bam_cycle == 1) {
      format_all(1);
      updateShiftRegister(write_data[row_cycle]);
    } else if (bam_cycle == 3) {
      format_all(2);
      updateShiftRegister(write_data[row_cycle]);
    } else if (bam_cycle == 7) {
      format_all(3);
      updateShiftRegister(write_data[row_cycle]);
    }
    bam_cycle = (bam_cycle+1) % 15;
    row_cycle = (row_cycle+1) % 4;
}

// Function to set pixel value in data array given RGB values
// Args: Pixel location (x,y) and 3-byte array of RGB color values
void setColor(int x, int y, const byte* color) {
  colors[x][Y_DIM - 1 - y][0] = color[0];
  colors[x][Y_DIM - 1 - y][1] = color[1];
  colors[x][Y_DIM - 1 - y][2] = color[2];
}

// Retrieve one of the 3 color bytes for a given pixel
// Args: Pixel location (x,y) and rgb = 0 for red, 1, for green, and 2 for blue
// Returns: One byte of the specified color in the given location
uint8_t getColor (int x, int y, int rgb) {
  return colors[x][Y_DIM - 1 - y][rgb];
}

void setupColors() {
  for (int x = 0; x < X_DIM; x++) {
    for (int y = 0; y < Y_DIM; y++) {
      if (x == 0) {
        setColor(x, y, green);
      } else if (x==1) {
        setColor(x, y, purple);
      } else if (x % 2 == 0) {  // For rows 0 and 2
        setColor(x, y, red);
      } else {  // For rows 1 and 3
        setColor(x, y, blue);
      }
    }
  }
}


void setup() {
  // Set pin modes
  pinMode(latchPin, OUTPUT);

  // Initialize SPI
  SPI.begin();
  myTimer.begin(timerCallback, 4); // Replace X with the desired interval in microseconds
  setupColors();

  Serial.begin(9600);
}

int color_index = 0;
int num_this_color = 0;
unsigned long lastTime = 0;
int pwm = 0;
void loop() {
  if (millis() - lastTime >= 150) {
      lastTime = millis();
      shiftSnakePattern(color_values[color_index]);
      num_this_color += 1;
      if (num_this_color >= 10) {
        color_index = (color_index+1) % NUM_COLORS;
        num_this_color = 0;
      }
  }
}

void updateShiftRegister(const byte* data) {
  // Ensure correct SPI settings for the shift register, using Mode 3
  SPI.beginTransaction(SPISettings(10000000, LSBFIRST, SPI_MODE0)); // Adjust the speed as necessary
  
  digitalWrite(latchPin, LOW); // Prepare to latch data
  for (int i = DATA_LEN - 1; i >= 0; i--) {
    SPI.transfer(data[i]);  // Send the current byte
  }
  digitalWrite(latchPin, HIGH); // Latch data into the output register
  
  SPI.endTransaction(); // End the SPI transaction
}

// Format data for all rows to be shifted onto shift registers
void format_all(int BAM_bit) {
  for (int i = 0; i < X_DIM; i++) {
    // Format each row
    format_data(i, BAM_bit);
  }
}

// Format one row of data for shifting onto shift registers, based on current bit of BAM
// Args: Row to format and current bit in BAM cycle
// Output: Formats one row of the raw data array
void format_data(int row, int BAM_cycle) {
  int curr_bit = 0; // Last filled bit (left to right)
  int curr_byte = 0; // Byte we are filling

  uint8_t working_byte = 0x00;

  for (int i = 0; i < Y_DIM; i++) {
    uint8_t combinedBits = 0; // Stores the 3 color bits for a given pixel for the current BAM bit
    uint8_t mask = 1 << BAM_cycle; // Used to decide if color is ON or OFF for this bit in cycle

    // Calculate current color bits
    for (int color = 0; color < 3; color++) {  // For RGB
      if (colors[row][i][color] & mask) {      // Shift and check the bit
        combinedBits |= 1 << (2 - color);      // Set bit for RGB
      }
    }

    if (curr_bit < 6) {
      working_byte = (working_byte << 3 | combinedBits);
      curr_bit += 3;
    } else if (curr_bit == 6) {
      working_byte = (working_byte<<2) | (combinedBits>>1);
      write_data[row][curr_byte] = working_byte;
      curr_byte += 1;
      curr_bit = 1;
      working_byte = (combinedBits & 0b00000001);
    } else if (curr_bit == 7) {
      working_byte = (working_byte<<1) | (combinedBits>>2);
      write_data[row][curr_byte] = working_byte;
      curr_byte += 1;
      curr_bit = 2;
      working_byte = (combinedBits & 0b00000011);
    } else if (curr_bit == 8) {
      write_data[row][curr_byte] = working_byte;
      curr_byte += 1;
      curr_bit = 0;
      working_byte = combinedBits;
    }
  }

  // Add row selection bits to data frame
  working_byte = working_byte<<(X_DIM + BIT_SEPARATOR); // Scoot over to make room for separator and row bits

  // Add row bit for multiplexing without modifying data bits
  uint8_t row_select = (~(0b1<<row) & (0xFF>>(8-X_DIM)));
  working_byte |= row_select;

  // Save the last byte
  write_data[row][curr_byte] = working_byte;
}

// Foundation of the snake animation
void shiftSnakePattern(const uint8_t newColor[3]) {
  // First row
  uint8_t temp_color[] = {0, 0, 0};
  temp_color[0] = getColor(0, 0, 0);
  temp_color[1] = getColor(0, 0, 1);
  temp_color[2] = getColor(0, 0, 2);
  setColor(0, 0, newColor);

  uint8_t temp_color_2[] = {0, 0, 0};
  for (int i = 1; i < X_DIM; i++) {
    temp_color_2[0] = temp_color[0];
    temp_color_2[1] = temp_color[1];
    temp_color_2[2] = temp_color[2];
    temp_color[0] = getColor(i, 0, 0);
    temp_color[1] = getColor(i, 0, 1);
    temp_color[2] = getColor(i, 0, 2);
    setColor(i, 0, temp_color_2);
  }

  for (int j = 1; j < Y_DIM; j++) {
    if (j%2 == 0) {
      temp_color_2[0] = temp_color[0];
      temp_color_2[1] = temp_color[1];
      temp_color_2[2] = temp_color[2];
      temp_color[0] = getColor(0, j, 0);
      temp_color[1] = getColor(0, j, 1);
      temp_color[2] = getColor(0, j, 2);
      setColor(0, j, temp_color_2);
      for (int i = 1; i < X_DIM; i++) {
        temp_color_2[0] = temp_color[0];
        temp_color_2[1] = temp_color[1];
        temp_color_2[2] = temp_color[2];
        temp_color[0] = getColor(i, j, 0);
        temp_color[1] = getColor(i, j, 1);
        temp_color[2] = getColor(i, j, 2);
        setColor(i, j, temp_color_2);
      }
    } else {
      temp_color_2[0] = temp_color[0];
      temp_color_2[1] = temp_color[1];
      temp_color_2[2] = temp_color[2];
      temp_color[0] = getColor(X_DIM-1, j, 0);
      temp_color[1] = getColor(X_DIM-1, j, 1);
      temp_color[2] = getColor(X_DIM-1, j, 2);
      setColor(X_DIM-1, j, temp_color_2);
      for (int i = X_DIM-2; i >= 0; i--) {
        temp_color_2[0] = temp_color[0];
        temp_color_2[1] = temp_color[1];
        temp_color_2[2] = temp_color[2];
        temp_color[0] = getColor(i, j, 0);
        temp_color[1] = getColor(i, j, 1);
        temp_color[2] = getColor(i, j, 2);
        setColor(i, j, temp_color_2);
      }
    }
  }
}
