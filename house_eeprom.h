#ifndef HOUSE_EEPROM_H
#define HOUSE_EEPROM_H

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
//
//  512k Flash layout is supposedly:
//
//  00000   app
//  7B000   _SPIFFS_end
//  7c000   Default Config
//  7e000   blank.bin
//

// eeprom 4k at 7B000

//
// EEPROM Contents are stored here - if you want to add fields add them at the end of
//  the typedef below and bump the version number there, they will be initialised to 0 
//  next time we start
//
//  start by getting a pointer to the structure with get_pointer() (that loads the data into sram)
//  if you change anything call changed(), always call flush() before deep sleeping
//

typedef struct eeprom_contents {
    unsigned char   magic;    // always 0xa5 - if it's not that we'll erase it
    unsigned char   version;
#define EEPROM_VERSION 0
    unsigned short  length; // length of this structure (by default we upgrade 
    // add your stuff here and 
} eeprom_contents;

class house_eeprom {
public:
  house_eeprom(int sc) { sector = sc; is_changed= 0; is_loaded = 0;}
  eeprom_contents *get_pointer();
  void changed();
  void flush();
private:
  int sector;
  bool is_loaded;
  bool is_changed;
  union {
    unsigned long _align; // force alignment
    eeprom_contents e;
    unsigned char _e[(sizeof(e)+3)&~3]; // round the storage up to a multiple of 4 bytes
  };
};

extern house_eeprom eeprom;

#endif

