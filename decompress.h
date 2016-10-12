#ifndef DECOMPRESS_HH
#define DECOMPRESS_HH
/*   
 *   Copyright (C) 2016 Paul Campbell

 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.

 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.

 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

      // humidity/temp compression - humidity is 7 bits 0-100, temp is 8 bits signed C
      // If bit 7 of byte 0 is 1 then bits 6:0 of byte0 is % humidity, byte1 is temp in C
      // if bit 7 of byte 0 is 0 then bits 6:4 are a signed diff of humidity from previous value, 3:0 are a signed diff of temp from previous value
      // if bits 7:4 of byte 0 are 1111 it is an escape (see below)
      // 
      // pressure compression
      // if bit 0 of byte0 is set bits 6:0 are a signed delta of the pressure in kPa
      // if bit 1 of byte0 is set bits 6:0 of byte0 is MSB of pressure, byte 1 is LSB of pressure in kPa
      // if bits 7:4 of byte 0 are 1111 it is an escape (see below)
      //
      // escapes
      //  1111 1111 - end of stream (unwritten FLASH)
      //  1111 0000 - time signature
      //  1111 0001 - time signature + change following samples to temp/humidity only (first following sample will be a full sample)
      //  1111 0010 - time signature + change following samples to pressure only (first Afollowing sample will be a full sample)
      //  1111 0011 - time signature + change following samples to temp/humidity followed by pressure (first following sample will be a full sample)
      //  1111 0100 - followed by 2 bytes MSB LSB, sampling rate in seconds (default is 60 seconds, one sample per minute)
      //  1111 0101 - followed by 0 terminated string comment
      //  1111 0110 - followed by a 1-byte value - 'mark' a user inserted mark in the stream (for example "I turned on the heater here")
      //  1111 0111 - null - ignored for padding
      //  1111 1000 - followed by 1-byte count 1-255 - previous value didn't change to N samples
      //  1111 1001-1110 undefined
      //
      // time signature:
      //  byte0 - bits 7:0 years since 2000 0-254 - value '0xff' means no time is known (bytes1-4 are missing)
      //  byte1 - bits 7:4 month 1-12,  bits 3:0 day MSB 1-31
      //  byte2 - bit 7 day LSB 1-23, bits 6:2 hour 0-23 bits 1:0 minutes msb
      //  byte3 - bits 7:4 minutes lsb 0-59, bits 3:0 - F means byte4 not included, otherwise undefined, set to 0
      //  byte4 - bits 7:6 undefined set to 0, bits 5:0 seconds

typedef struct time_stamp {
  unsigned char valid;
  int year;
  unsigned char month;
  unsigned char day;
  unsigned char hour;
  unsigned char minute;
  unsigned char second; 
} time_stamp;

#ifdef __cplusplus
extern "C"
{
#endif
void log_data(time_stamp *t, unsigned char valid_th, int temp, int humidity, unsigned char valid_p, int pressure);
int get_compressed_byte(int offset);
void log_mark(time_stamp *t, int mark);
int dump_rtc_data(void);
#ifdef __cplusplus
}
#endif

#endif
