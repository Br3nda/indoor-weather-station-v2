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

#include <Wire.h>

#include "PC8563.h"

#define ADDRESS 0x51 

PC8563::PC8563()
{
  Wire.begin();
}
  
bool
PC8563::begin()
{
  Wire.beginTransmission((uint8_t)ADDRESS);
  Wire.write((uint8_t)0);
  Wire.endTransmission();

  Wire.requestFrom((uint8_t)ADDRESS, (uint8_t)1);
  if (!Wire.available()) 
	  return false;
  return true;
}

bool
PC8563::read(pc_time &tm)
{
  Wire.beginTransmission((uint8_t)ADDRESS);
  Wire.write((uint8_t)2);
  Wire.endTransmission();
  Wire.requestFrom((uint8_t)ADDRESS, (uint8_t)7);	
  if (!Wire.available())
	  return false;
  int v = Wire.read();
  if (v&0x80)
	  return false;
  tm.second= (v&0xf) + (10*((v>>4)&0x7));
  v = Wire.read();
  tm.minute = (v&0xf) + (10*((v>>4)&0x7));
  v = Wire.read();
  tm.hour = (v&0xf) + (10*((v>>4)&0x3));
  v = Wire.read();
  tm.day = (v&0xf) + (10*((v>>4)&0x3));
  v = Wire.read();
  v = Wire.read();
  tm.month = (v&0xf) + (10*((v>>4)&0x1));
  tm.year = (v&0x80?2100:2000);
  v = Wire.read();
  tm.year += (v&0xf) + (10*((v>>4)&0xf));
  return true;
}
  
bool
PC8563::write(pc_time &tm)
{
  Wire.beginTransmission((uint8_t)ADDRESS);
  Wire.write((uint8_t)0);
  Wire.write((uint8_t)0x20);	// stop
  Wire.write((uint8_t)0x00);	// no alarm
  Wire.write((uint8_t)((tm.second%10)|((tm.second/10)<<4)));
  Wire.write((uint8_t)((tm.minute%10)|((tm.minute/10)<<4)));
  Wire.write((uint8_t)((tm.hour%10)|((tm.hour/10)<<4)));
  Wire.write((uint8_t)((tm.day%10)|((tm.day/10)<<4)));
  Wire.write((uint8_t)1); // don't care
  Wire.write((uint8_t)((tm.month%10)|((tm.month/10)<<4)|(tm.year>=2100?0x80:0)));
  int v = tm.year-2000;
  if (v >= 100)
	  v -= 100;
  Wire.write((uint8_t)((v%10)|((v/10)<<4)));
  Wire.endTransmission();
  Wire.beginTransmission((uint8_t)ADDRESS);
  Wire.write((uint8_t)0);
  Wire.write((uint8_t)0); // start
  Wire.endTransmission();
  return true;
}
PC8563 PC8563_RTC;
