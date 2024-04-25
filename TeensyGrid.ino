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

// Animation tracking variables
int color_index = 0;
int num_this_color = 0;
unsigned long lastTime = 0;

// Defining some standard colors
int NUM_COLORS = 7;
uint8_t color_values[][3] = { { 0xF, 0x0, 0x0 }, // Red
                              { 0xF, 0x3, 0x0 }, // Orange
                              { 0xF, 0x8, 0x0 }, // Yellow
                              { 0x0, 0xF, 0x0 }, // Green
                              { 0x0, 0x0, 0xF }, // Blue
                              { 0x8, 0x0, 0xF }, // Purple
                              { 0xF, 0x1, 0x3}}; // Pink

// Timer with interrupts used for multiplexing and BAM
IntervalTimer myTimer;

// Timer interrupt handler to multiplex and perform BAM
// 1 of 4 rows is lit during each execution
// Individual LEDs/colors are ON/OFF based on their BAM cycle
void timerCallback() {
  if (bam_cycle == 0) { // BAM bit 0
      format_all(0); // Format data for output
      updateShiftRegister(write_data[row_cycle]); // Write to display
  } else if (bam_cycle == 1) { // BAM bit 1
      format_all(1); // Format data
      updateShiftRegister(write_data[row_cycle]); // Write data
  } else if (bam_cycle == 3) { // BAM bit 2
      format_all(2); // Format data
      updateShiftRegister(write_data[row_cycle]); // Write data
  } else if (bam_cycle == 7) { // BAM bit 3
      format_all(3); // Format data
      updateShiftRegister(write_data[row_cycle]); // Write data
  }

  bam_cycle = (bam_cycle+1) % 15; // Increment BAM bit
  row_cycle = (row_cycle+1) % 4; // Increment currently lit row
}

// Function to set pixel value in data array given RGB values
// Args: Pixel location (x,y) and 3-byte array of RGB color values
void setColor(int x, int y, const byte* color) {
  colors[x][Y_DIM - 1 - y][0] = color[0]; // Red
  colors[x][Y_DIM - 1 - y][1] = color[1]; // Green
  colors[x][Y_DIM - 1 - y][2] = color[2]; // Blue
}

// Retrieve one of the 3 color bytes for a given pixel
// Args: Pixel location (x,y) and rgb = 0 for red, 1, for green, and 2 for blue
// Returns: One byte of the specified color in the given location
uint8_t getColor (int x, int y, int rgb) {
  return colors[x][Y_DIM - 1 - y][rgb];
}

// Setup SPI and output pins
void setup() {
  // Set pin modes
  pinMode(latchPin, OUTPUT);

  // Initialize SPI
  SPI.begin();
  myTimer.begin(timerCallback, 4); // 4 us period -> 250 kHz frequency

  // Begin serial communications with a baud rate of 9600
  Serial.begin(9600);
}

// Grand loop to run animations
void loop() {
  // Update animation every 150 milliseconds
  if (millis() - lastTime >= 150) {
      lastTime = millis(); // Set time for future reference
      shiftSnakePattern(color_values[color_index]); // Shift current animation (Snake)

      // Shift in a certain number before changing the input color
      num_this_color += 1; // Increment color counter
      if (num_this_color >= 10) { // Shift in no more than 10
        color_index = (color_index+1) % NUM_COLORS; // Move to next color if >= 10
        num_this_color = 0; // Reset color counter
      }
  }
}

// Function to shift data onto shift registers
// Args: An array of bytes, data
// Output: Shifts byte array onto shift registers and in turn the display
void updateShiftRegister(const byte* data) {
  // Begin SPI transaction at 10MHz
  SPI.beginTransaction(SPISettings(10000000, LSBFIRST, SPI_MODE0));
  
  digitalWrite(latchPin, LOW); // Prepare to latch data

  // Transfer all data bytes
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

  // Initialize working byte
  // This gets put into write_data and then reused when full
  uint8_t working_byte = 0x00;

  // Iterate over rows
  for (int i = 0; i < Y_DIM; i++) {
    uint8_t combinedBits = 0; // Stores the 3 color bits for a given pixel for the current BAM bit
    uint8_t mask = 1 << BAM_cycle; // Used to decide if color is ON or OFF for this bit in cycle

    // Calculate current color bits
    for (int color = 0; color < 3; color++) {  // For RGB
      if (colors[row][i][color] & mask) {      // Shift and check the bit
        combinedBits |= 1 << (2 - color);      // Set bit for RGB
      }
    }

    // If there is room in the working byte, append the color bits
    if (curr_bit < 6) {
      working_byte = (working_byte << 3 | combinedBits); // Add color bits
      curr_bit += 3; // Advance bit position
    // If 2 empty bits, put 2 color bits in one byte and 1 in the next
    } else if (curr_bit == 6) {
      working_byte = (working_byte<<2) | (combinedBits>>1); // Add color bits
      write_data[row][curr_byte] = working_byte; // Save to write array
      curr_byte += 1; // Advance byte position
      curr_bit = 1; // Advance bit position
      working_byte = (combinedBits & 0b00000001); // Save remaining data to next byte
    // If 1 empty bit, put 1 color bit in one byte and 2 in the next
    } else if (curr_bit == 7) {
      working_byte = (working_byte<<1) | (combinedBits>>2); // Add final color bit
      write_data[row][curr_byte] = working_byte; // Save to write array
      curr_byte += 1; // Advance byte position
      curr_bit = 2; // Advance bit position
      working_byte = (combinedBits & 0b00000011); // Save remaining bits to working byte
    // If byte is full, save byte to write_data and start new working byte
    } else if (curr_bit == 8) {
      write_data[row][curr_byte] = working_byte; // Save full byte to write array
      curr_byte += 1; // Advance byte position
      curr_bit = 0; // Reset bit position
      working_byte = combinedBits; // Save all color bits to new working bit
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
  // Temporary variables to hold color values during shifting
  uint8_t temp_color[] = {0, 0, 0};
  uint8_t temp_color_2[] = {0, 0, 0};

  // Shift new color into first pixel
  temp_color[0] = getColor(0, 0, 0); // Red
  temp_color[1] = getColor(0, 0, 1); // Green
  temp_color[2] = getColor(0, 0, 2); // Blue
  setColor(0, 0, newColor); // Write to color array

  // Shift remainder of first row to the right
  for (int i = 1; i < X_DIM; i++) {
    // Save current pixel to temp_color_2 temporarily
    temp_color_2[0] = temp_color[0];
    temp_color_2[1] = temp_color[1];
    temp_color_2[2] = temp_color[2];

    // Fetch color of next pixel
    temp_color[0] = getColor(i, 0, 0);
    temp_color[1] = getColor(i, 0, 1);
    temp_color[2] = getColor(i, 0, 2);

    // Set current pixel to color of previous pixel
    setColor(i, 0, temp_color_2);
  }

  // Process each row in the matrix
  for (int j = 1; j < Y_DIM; j++) {
    // Shift even rows right
    if (j % 2 == 0) {
      // Transfer color from end of previous row to start of current row
      temp_color_2[0] = temp_color[0];
      temp_color_2[1] = temp_color[1];
      temp_color_2[2] = temp_color[2];

      // Save color of first pixel in the row
      temp_color[0] = getColor(0, j, 0);
      temp_color[1] = getColor(0, j, 1);
      temp_color[2] = getColor(0, j, 2);
      
      // Set color of first pixel in current row
      setColor(0, j, temp_color_2);

      // Shift all other pixels in the row to the right
      for (int i = 1; i < X_DIM; i++) {
        // Store previous color
        temp_color_2[0] = temp_color[0];
        temp_color_2[1] = temp_color[1];
        temp_color_2[2] = temp_color[2];
        
        // Store current color
        temp_color[0] = getColor(i, j, 0);
        temp_color[1] = getColor(i, j, 1);
        temp_color[2] = getColor(i, j, 2);

        // Set new color for current pixel
        setColor(i, j, temp_color_2);
      }
    // Shift odd rows left
    } else {
      // Save color from the end of the previous row
      temp_color_2[0] = temp_color[0];
      temp_color_2[1] = temp_color[1];
      temp_color_2[2] = temp_color[2];

      // Save color from current pixel
      temp_color[0] = getColor(X_DIM-1, j, 0);
      temp_color[1] = getColor(X_DIM-1, j, 1);
      temp_color[2] = getColor(X_DIM-1, j, 2);

      // Set current pixel to color of final pixel in previous row
      setColor(X_DIM-1, j, temp_color_2);

      // Shift all remaining pixels in the row to the left
      for (int i = X_DIM-2; i >= 0; i--) {
        // Save previous color
        temp_color_2[0] = temp_color[0];
        temp_color_2[1] = temp_color[1];
        temp_color_2[2] = temp_color[2];

        // Save current color
        temp_color[0] = getColor(i, j, 0);
        temp_color[1] = getColor(i, j, 1);
        temp_color[2] = getColor(i, j, 2);

        // Shift previous pixel's color onto current pixel
        setColor(i, j, temp_color_2);
      }
    }
  }
}
