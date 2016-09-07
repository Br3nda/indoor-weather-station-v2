#ifndef PC8563_h
#define PC8563_h
/*
 * PC8563.c - library for PC8563.c RTC
  
  Copyright (c) Paul Campbell 2016
  This library is intended to be uses with Arduino Time library functions
  The library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.
  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.
  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
  
  6 Aug 2016 - Initial release
 */

typedef struct {
	uint8_t second;
	uint8_t minute;
	uint8_t hour;
	uint8_t day;
	uint8_t month;
	uint16_t year;
} pc_time;

class PC8563 {
public:
	PC8563();
	bool begin();
	bool read(pc_time &tm);
	bool write(pc_time &tm);
};
extern PC8563 PC8563_RTC;
#endif
