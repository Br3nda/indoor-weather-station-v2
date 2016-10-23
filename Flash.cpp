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
#include "Flash.h"


//
//  we have N pages of SPI_FLASH_SEC_SIZE bytes
//
//  each page starts with a 4 byte ref number - FFFFFFFF means empty, every time we allocate a page
//  we give it an increasing ref number, they are allocated in order going backwards (so we can grow the
//  code), they wrap, then overflow to external flash where the same thing happens (not yet implemented)
//
//  we can find the first record simply by searching thru the pages
//
//  8266 flash operations must be 4 byte aligned, and a multiple of 4 bytes.
//
//  within each page there are a bunch of 'records', each is self contained (in the sense that compression 
//  restarts on each), they start with a length byte followed by data (max 255 bytes) the length 
//  is the actual length of the embedded data, records are 4 byte aligned and padded out to 4 byte boundaries
//  a record with a length of 0 indicates the end of the last record in the page
//
