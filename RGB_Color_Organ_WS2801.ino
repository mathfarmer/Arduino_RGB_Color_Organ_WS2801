/*
RGB Color Organ W2801

Takes amplitude information sent over a serial connection and lights 
up RGB LEDs controlled by the WS2801 chipset based on that information.

Copyright (C) 2013 Douglas A. Telfer

This source code is released simultaneously under the GNU GPL v2 
and the Mozilla Public License, v. 2.0; derived works may use 
either license, a compatible license, or both licenses as suits 
the needs of the derived work.

Additional licensing terms may be available; contact the author
with your proposal.

*** GNU General Public License, version 2 notice:

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

*** Mozilla Public License, v. 2.0 notice:

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at http://mozilla.org/MPL/2.0/
*/

#include "SPI.h"
#include "Adafruit_WS2801.h" // https://github.com/adafruit/Adafruit-WS2801-Library

#define LED_COUNT       25   // If LED_COUNT > 255, promote counter to int
#define BYTES_PER_COLOR 3    // Only applies to color data, not output. Code needs to be rewritten if you change this
#define MAX_COLORS      85   // If MAX_COLORS*BYTES_PER_COLOR > 255, promote col_idx to int 
#define MAX_AMPLITUDES  85   // If MAX_AMPLITUDES*BYTES_PER_COLOR > 255, promote out_idx and output_bytes to int
                             // If MAX_AMPLITUDES > 255 or MAX_COLORS > 255 protocol needs to be modified

enum { ERROR_BYTE      = 'e',  // Something went wrong: device is now in command mode          -> 'm'
       MODE_COMMAND    = 'm',  // Next byte is a command                                       -> mode from byte
       FLAG_OFFSET_ON  = 'O',  // Expect beat offset at the start of amp bytes.                -> 'm'
       FLAG_OFFSET_OFF = 'o',  // Do not expect beat offset at the start of amp bytes          -> 'm'
       MODE_INIT_COLOR = 'c',  // Receive color data - Next byte is a color byte               -> 'C' or 'm'
       MODE_GET_COLORS = 'C',  // INTERNAL: Receive color data - Next byte is a color byte     -> 'C' or 'm'
       MODE_INIT_AMPS  = 'a',  // Init amplitude data - Next byte is the number of amplitudes  -> 'K' or 'A' or 'm'
       MODE_GET_OFFSET = 'K',  // INTERNAL: Receive offset number - Next byte                  -> 'A'
       MODE_GET_AMPS   = 'A'}; // INTERNAL: Receive amplitude data - Next byte is an amplitude -> 'A' or 'a'

inline boolean checkMode( byte proposedMode ) {
  return    proposedMode == MODE_COMMAND
         || proposedMode == MODE_INIT_COLOR
      // || proposedMode == MODE_GET_COLORS
         || proposedMode == MODE_INIT_AMPS
      // || proposedMode == MODE_GET_OFFSET
      // || proposedMode == MODE_GET_AMPS
         ;
}

inline boolean checkCommand( byte proposedCommand ) {
  return    proposedCommand == FLAG_OFFSET_ON
         || proposedCommand == FLAG_OFFSET_OFF
         ;
}

byte mode;
byte counter;
byte col_idx;
byte colors[MAX_COLORS*BYTES_PER_COLOR];
byte colors_len;
byte amp_idx;
byte amplitudes[MAX_AMPLITUDES];
byte amplitudes_len;
byte offset;
boolean expectOffset;

uint8_t dataPin  = 12;    // Yellow wire on Adafruit Pixels
uint8_t clockPin = 13;    // Green wire on Adafruit Pixels

Adafruit_WS2801 strip = Adafruit_WS2801(LED_COUNT, dataPin, clockPin);
//boolean flip; // Used for crude a/b profiling

void setup()
{
  strip.begin();
  strip.show(); // Initialize all pixels to 'off'
  Serial.begin(115200);
  counter = 0;
  mode = MODE_COMMAND;
  colors_len = 0;
  amplitudes_len = 0;
  offset = 0;
  expectOffset = false;
  //flip = false;
  
  Serial.write(MODE_COMMAND);
}

void loop() {
  byte inByte;
  int mungedByte;
  
  mungedByte = Serial.read(); 
  if (mungedByte >= 0) {
    inByte = lowByte(mungedByte);
    
    switch (mode) {
      case MODE_COMMAND:
        if (checkMode(inByte)) {
          mode = inByte;
          counter = 0;
          Serial.write(inByte);
        }
        else if (checkCommand(inByte)) {
          switch (inByte) {
            case FLAG_OFFSET_ON:
              expectOffset = true;
              break;
            case FLAG_OFFSET_OFF:
              expectOffset = false;
              break;
          }
          Serial.write(inByte);
        }
        else {
          Serial.write(ERROR_BYTE);
          mode = MODE_COMMAND;
        }
        break;
      case MODE_INIT_COLOR:
        if (inByte == 0) {
          mode = MODE_COMMAND;
          Serial.write(MODE_COMMAND);
        }
        else {
          colors_len = inByte * BYTES_PER_COLOR;
          if (colors_len > MAX_AMPLITUDES * BYTES_PER_COLOR) {
            Serial.write(ERROR_BYTE);
            mode = MODE_COMMAND;
          }
          else {
            mode = MODE_GET_COLORS;
          }
        }
        break;
      case MODE_GET_COLORS:
        colors[counter] = inByte;
        ++counter;
        if (counter >= colors_len) {
          counter = 0;
          mode = MODE_INIT_COLOR;
          Serial.write(MODE_INIT_COLOR);
        }
        break;
      case MODE_INIT_AMPS:
        if (inByte == 0) {
          mode = MODE_COMMAND;
          Serial.write(MODE_COMMAND);
        }
        else {
          amplitudes_len = inByte;
          if (amplitudes_len > MAX_AMPLITUDES || amplitudes_len > colors_len) {
            Serial.write(ERROR_BYTE);
            amplitudes_len = 0;
            mode = MODE_COMMAND;
          }
          else {
            amp_idx = 0;
            col_idx = 0;
            counter = 0;
            if (expectOffset)
              mode = MODE_GET_OFFSET;
            else
              mode = MODE_GET_AMPS;
          }
        }
        break;
      case MODE_GET_OFFSET:
        offset = inByte;
        mode = MODE_GET_AMPS;
        break;  
      case MODE_GET_AMPS:
        if (amp_idx < amplitudes_len) {
          amplitudes[amp_idx++] = inByte;
        }
        if (amp_idx >= amplitudes_len) {
          amp_idx = offset;
          col_idx = BYTES_PER_COLOR*offset;
          counter = 0;
          while (counter < LED_COUNT) {
            byte r = highByte(amplitudes[amp_idx] * colors[col_idx++]);
            byte g = highByte(amplitudes[amp_idx] * colors[col_idx++]);
            byte b = highByte(amplitudes[amp_idx] * colors[col_idx++]);
            amp_idx++;
            strip.setPixelColor(counter, Color(r, g, b));
            
            if (col_idx >= BYTES_PER_COLOR*amplitudes_len)
              col_idx -= BYTES_PER_COLOR*amplitudes_len;
            if (amp_idx >= amplitudes_len)
              amp_idx -= amplitudes_len;
              
            counter++;
          }

          strip.show();
            
          //flip = !flip;
          mode = MODE_INIT_AMPS;
          Serial.write(MODE_INIT_AMPS);
        }
        break;
      default:
        Serial.write(ERROR_BYTE);
        mode = MODE_COMMAND;
        break;
    } 
  }
}

// Create a 24 bit color value from R,G,B
uint32_t Color(byte r, byte g, byte b)
{
  uint32_t c;
  c = r;
  c <<= 8;
  c |= g;
  c <<= 8;
  c |= b;
  return c;
}
