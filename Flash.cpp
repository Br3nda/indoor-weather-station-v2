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
//  each page starts with a 4 byte magic number followed by a 4 byte ref number - FFFFFFFF means empty, 
//  every time we allocate a page we give it an increasing ref number, they are allocated in order going 
//  backwards (so we can grow the code), they wrap, then overflow to external flash where the same thing 
//  happens (not yet implemented)
//
//  we can find the first record simply by searching thru the pages
//
//  8266 flash operations must be 4 byte aligned, and a multiple of 4 bytes.
//
//  within each page there are a bunch of 'records', each is self contained (in the sense that compression 
//  restarts on each), they start with a length byte followed by data (max 255 bytes) the length 
//  is the actual length of the embedded data -1 (so values 0-254 representing 1-255, a value of 
//  255 is the last as yet unused record), records are 4 byte aligned and padded out to 4 byte boundaries
//


#define SEC_MAX_DATA (SPI_FLASH_SEC_SIZE-sizeof(flash_page_header))



HomeFlash flash;

void
HomeFlash::DoInit()
{
  flash_page_header h;
  unsigned int last_ref;
printf("doinit\n");
  current_page_address = first_page_address = (FLASH_LAST*SPI_FLASH_SEC_SIZE);
  spi_flash_read(current_page_address, (unsigned int *)&h, sizeof(h));
  last_ref = h.ref;
  if (h.magic == FLASH_MAGIC && h.ref != 0xffffffff) {
printf("doinit 1\n");
    unsigned int  address = (FLASH_FIRST*SPI_FLASH_SEC_SIZE);
    int last_page_address = current_page_address;
    spi_flash_read(address, (unsigned int *)&h, sizeof(h));
    while (h.magic == FLASH_MAGIC && h.ref == (last_ref-1)) {
        last_ref = h.ref;
        last_page_address = address;
        address += SPI_FLASH_SEC_SIZE;
        spi_flash_read(address, (unsigned int *)&h, sizeof(h));
    }
    first_page_address = last_page_address;
  } else {
printf("doinit 2\n");
    for (int i = 0; i < (FLASH_LAST-FLASH_FIRST+1); i++) {
      spi_flash_read(current_page_address, (unsigned int *)&h, sizeof(h));
      if (h.magic == FLASH_MAGIC && h.ref != 0xffffffff) 
        break;
      current_page_address -= SPI_FLASH_SEC_SIZE;
    }
    if (current_page_address < (FLASH_FIRST*SPI_FLASH_SEC_SIZE)) {  // empty - make an initial empty record
printf("doinit - empty\n");
      current_page_address = first_page_address = (FLASH_LAST*SPI_FLASH_SEC_SIZE);
      
      EraseSector(current_page_address/SPI_FLASH_SEC_SIZE);
      
      h.magic = FLASH_MAGIC;
      h.ref = 0;
      next_ref = 1;
printf("doinit - writing data 0x%x\n", current_page_address);
      noInterrupts();
      if (spi_flash_write(current_page_address, reinterpret_cast<uint32_t*>(&h), sizeof(h)) != SPI_FLASH_RESULT_OK) {
        interrupts();
printf("doinit - writing data retry 0x%x\n", current_page_address);
        noInterrupts(); // retry
        spi_flash_write(current_page_address, reinterpret_cast<uint32_t*>(&h), sizeof(h));
      }
      interrupts();
      first_page_offset = current_page_offset = 0;
      goto done;
    }
    first_page_address = current_page_address;
  }
printf("doinit a fpa=0x%x cpa = 0x%x\n", first_page_address, current_page_address);
  for (;;) {
      unsigned int address = current_page_address-SPI_FLASH_SEC_SIZE;
      if (address < (FLASH_FIRST*SPI_FLASH_SEC_SIZE))
        break;
      spi_flash_read(address, (unsigned int *)&h, sizeof(h));
      if (h.magic != FLASH_MAGIC || h.ref != (last_ref+1))
        break;
     current_page_address = address;
     last_ref = h.ref;
  }
printf("doinit b fpa=0x%x cpa = 0x%x\n", first_page_address, current_page_address);
  next_ref = h.ref+1;
  // now search for end of page
  current_page_offset = 0;
  for (;;) {
      unsigned char v;
      if (current_page_offset >= SEC_MAX_DATA)
        break;
      v = read_length(current_page_address+sizeof(flash_page_header)+current_page_offset);
      if (v == 0xff)
        break;
      current_page_offset += (v+2+3)&~3;
  }
done:
printf("doinit done\n");
  next_page_address = first_page_address;
  next_page_offset = first_page_offset;
  init = 1;
}

bool 
HomeFlash::WriteRecord(unsigned char *p, int len) // write a record len <= 255
{
  union {
      unsigned char b[256];
      unsigned int align;
  }b;

printf("write record %d bytes\n", len);
  if (len <= 0 || len > 255)
    return 0;
  if (!init)
    DoInit();
  int sz = (1+len+3)&~3;
  b.b[0] = len-1;
  memcpy(&b.b[1], p, len);
  for (int i = len+1; i < len; i++)
    b.b[i] = 0xff;
  if (full) {
    printf("data is full\n");
    return 0;
  }
  if ((current_page_offset+sz) > SEC_MAX_DATA) { // current page is full move to the next 
    unsigned int n;
    
    if (current_page_address == (FLASH_FIRST*SPI_FLASH_SEC_SIZE)) {
      n = FLASH_LAST*SPI_FLASH_SEC_SIZE;
    } else {
      n = current_page_address-SPI_FLASH_SEC_SIZE;
    }
    if (n == first_page_address) {
      full = 1;
      printf("data is full\n");
      // here's where we overflow into external flash
      return 0;
    } 
    current_page_address = n;
    EraseSector(current_page_address/SPI_FLASH_SEC_SIZE);
    flash_page_header h;
    h.magic = FLASH_MAGIC;
    h.ref = next_ref++;
printf("writing header 0x%x\n", current_page_address);
    noInterrupts();
    if (spi_flash_write(current_page_address, reinterpret_cast<uint32_t*>(&h), sizeof(h)) != SPI_FLASH_RESULT_OK) {
      interrupts();
      noInterrupts(); // retry
      if (spi_flash_write(current_page_address, reinterpret_cast<uint32_t*>(&h), sizeof(h))!= SPI_FLASH_RESULT_OK) {
        interrupts();
        return 0;
      }
    }
    interrupts();
    current_page_offset = 0;
  }
printf("writing %d bytes to 0x%x\n", sz, current_page_address+current_page_offset);
  noInterrupts();
  if (spi_flash_write(current_page_address+sizeof(flash_page_header)+current_page_offset, reinterpret_cast<uint32_t*>(&b.b[0]), sz) != SPI_FLASH_RESULT_OK) {
    interrupts();
    noInterrupts(); // retry
    if (spi_flash_write(current_page_address+sizeof(flash_page_header)+current_page_offset, reinterpret_cast<uint32_t*>(&b.b[0]), sz) != SPI_FLASH_RESULT_OK) {
      interrupts();
      return 0;
    }
  }
  interrupts();
  current_page_offset += sz;
  return 1;
}

unsigned int 
HomeFlash::LoadBuffer(unsigned char *p, int max_len)
{
  union {
      unsigned int align;
      unsigned char b[256];
  } b;
  int r = 0;
  if (!init)
    DoInit();
  for (;;) { // Loop over pages
    while(r < max_len) { // Loop over records in page
      unsigned char sz;
      if (next_page_offset >= SEC_MAX_DATA) {
        next_page_offset = 0;
        break;
      }
      // Read in first four bytes, which include the size field
      spi_flash_read(next_page_address+sizeof(flash_page_header)+next_page_offset, (unsigned int *)&b, 4);
      sz = b.b[0];
      if (sz == 0xff)
        break;
      sz += 1;

      // Don't return partial records
      if (r + sz > max_len)
          break;

      // Read remaining bytes
      int inc = (sz+1+3)&~3;
      if (inc > 4)
        spi_flash_read(next_page_address+sizeof(flash_page_header)+next_page_offset+4, (unsigned int *)&b.b[4], inc-4);
      memcpy(p, &b.b[1], sz);
      p += sz;
      r += sz;
      next_page_offset += inc;
    }
    if (next_page_address == current_page_address)
      return r|FLASH_END_MARKER;

    next_page_offset = 0;
    if (next_page_address == (FLASH_FIRST*SPI_FLASH_SEC_SIZE)) {
      next_page_address = FLASH_LAST*SPI_FLASH_SEC_SIZE;
    } else {
      next_page_address = next_page_address-SPI_FLASH_SEC_SIZE;
    }
  }
}

void 
HomeFlash::CommitBuffer(void)
{
  if (!init)
    DoInit();
  for (;;) {
      if (first_page_address==current_page_address || first_page_address==next_page_address)
        break;
      if (first_page_address == (FLASH_FIRST*SPI_FLASH_SEC_SIZE)) {
        first_page_address = FLASH_LAST*SPI_FLASH_SEC_SIZE;
      } else {
        first_page_address = first_page_address-SPI_FLASH_SEC_SIZE;
      }
      noInterrupts();
      spi_flash_erase_sector(first_page_address/SPI_FLASH_SEC_SIZE);
      interrupts();
  }
  first_page_address = next_page_address;
  first_page_offset = next_page_offset;
}

void
HomeFlash::EraseSector(unsigned short s)
{
  unsigned int *p;
  unsigned int address = s*SPI_FLASH_SEC_SIZE;
  unsigned int b[256/sizeof(unsigned long)];
  printf("erase sector %d address 0x%x\n", s, address);
  for (int i = 0; i < SPI_FLASH_SEC_SIZE; i+=sizeof(b)) {
    spi_flash_read(address+i, &b[0], sizeof(b));
    p = &b[0];
    for (int j = 0; j < (sizeof(b)/sizeof(b[0])); j++)
    if (*p++ != ~0UL) {
      printf("actual erase i=%d @%x %x %x %x\n", i, (int)p,p[-1],p[0], p[1]);
      noInterrupts(); 
      spi_flash_erase_sector(s);
      interrupts();
      return;
    }
  }
}

void 
HomeFlash::Erase(void)
{
  flash_page_header fh, *fhp;
  for (unsigned int sector = FLASH_FIRST; sector <= FLASH_LAST; sector++) 
    EraseSector(sector);
  first_page_address = next_page_address = current_page_address = FLASH_LAST*SPI_FLASH_SEC_SIZE;
  first_page_offset = next_page_offset = current_page_address = 0;
  init = 0;
}

void 
HomeFlash::Dump(void)
{
  if (!init)
    DoInit();
  unsigned int offset = first_page_offset;
  unsigned int address = first_page_address;
  for (;;) {
    flash_page_header h;
    spi_flash_read(address, (unsigned int *)&h, sizeof(h));
    if (h.magic != FLASH_MAGIC || h.ref == 0xffffffff)
      break;
    printf("page @0x%x - ref=0x%x\n", address, h.ref);
    for (;;) {
      unsigned char v;
      if (offset >= SEC_MAX_DATA)
        break;
      v = read_length(address+sizeof(flash_page_header)+offset);
      if (v == 0xff)
        break;
      int len = v+1;
      printf("  %d: len=%d\n", offset, len);
      offset += (len+1+3)&~3;
    }
    if (address == current_page_address)
      break;
    if (address ==  (FLASH_FIRST*SPI_FLASH_SEC_SIZE)) {
      address = (FLASH_LAST*SPI_FLASH_SEC_SIZE);
    } else {
      address -= SPI_FLASH_SEC_SIZE;
    }
    offset = 0;
  }
}

unsigned char
HomeFlash::read_length(unsigned int offset)
{
  union {
      unsigned int v;
      unsigned char b[4];  
  }b;
  spi_flash_read(offset, &b.v, sizeof(b.v));
  return b.b[0];
}

