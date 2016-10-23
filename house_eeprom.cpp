
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
#include "Arduino.h"

extern "C" {
#include "c_types.h"
#include "ets_sys.h"
#include "os_type.h"
#include "osapi.h"
#include "spi_flash.h"
}
#include "house_eeprom.h"

//
//  512k Flash layout is supposedly:
//
//  00000   app
//  7B000   _SPIFFS_end
//  7c000   Default Config
//  7e000   blank.bin
//

// eeprom 4k at 7B000

house_eeprom eeprom(((512*1024)-(2*8*1024)-SPI_FLASH_SEC_SIZE)/SPI_FLASH_SEC_SIZE);


eeprom_contents* house_eeprom::get_pointer()
{
    
    if (!is_loaded) {
      int sz = (sizeof(e)+3)&~3;

      //printf("loading %d\n",sector);delay(1000);
      noInterrupts();
      spi_flash_read(sector * SPI_FLASH_SEC_SIZE, reinterpret_cast<uint32_t*>(&e), sz);
      interrupts();
      if (e.magic != 0xa5) {
        //printf("BAD load\n");delay(1000);
        memset(&e, 0, sizeof(e));   
        e.magic = 0xa5;
        e.version =  EEPROM_VERSION;
        e.length = sizeof(e);
        is_changed = 1;
      } else {
        //printf("GOOD load version = %d\n", e.version);delay(1000);
        is_changed = 0;
        if (e.length != sizeof(e)) {
          unsigned char *ep = (unsigned char *)&e;
          memset(&ep[e.length], 0, sizeof(e)-e.length);
          e.length = sizeof(e);
        }
        if (e.version != EEPROM_VERSION) {
          // put any code required to go from one version to another here
          e.version = EEPROM_VERSION;
        }
      }
      is_loaded = 1;
    }
    return &e;
}

void
house_eeprom::changed()
{
  if(is_loaded)
      is_changed = true;
}

void
house_eeprom::flush()
{
  if (!is_loaded || !is_changed) 
    return;
  int sz = (sizeof(e)+3)&~3;
  noInterrupts();
  if (spi_flash_erase_sector(sector) == SPI_FLASH_RESULT_OK) {
    if (spi_flash_write(sector * SPI_FLASH_SEC_SIZE, reinterpret_cast<uint32_t*>(&e), sz) == SPI_FLASH_RESULT_OK) {
      is_changed = 0;
    }
  }
  interrupts();
}

