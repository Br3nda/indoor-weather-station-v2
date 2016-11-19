#ifndef FLASH__H__
#define FLASH__H__
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

typedef struct flash_page_header {
  unsigned int magic;
#define FLASH_MAGIC 0xf1a5600d
  unsigned int ref;
} flash_page_header;

extern "C" {
extern unsigned char _irom0_text_end;
};

#define FLASH_FIRST ((((unsigned)&_irom0_text_end)-0x40200000+SPI_FLASH_SEC_SIZE-1)/SPI_FLASH_SEC_SIZE)
#define FLASH_LAST (((512*1024)-(2*8*1024)-2*SPI_FLASH_SEC_SIZE)/SPI_FLASH_SEC_SIZE)

class HomeFlash {
public:
  
  void _initHomeFlash() { init = 0; first_page_offset = 0;} // only call when calling before ctors are called out
  HomeFlash() {_initHomeFlash();}
  bool WriteRecord(unsigned char *p,int len); // write a record len <= 255
#define FLASH_END_MARKER 0x80000000
  unsigned int LoadBuffer(unsigned char *p, int max_len); 

  void SetRememberedOffset(int o) { first_page_offset = o; }
  unsigned int GetRememberedOffset() { return first_page_offset; }
  void CommitBuffer(void);
  void UnCommitBuffer(void) {next_page_address=first_page_address;next_page_offset=first_page_offset;};
  void Erase(void);
  void Dump(void);
private:
  bool init;
  bool full;
  void DoInit();
  void EraseSector(unsigned short s);
  unsigned char read_length(unsigned int offset);
  unsigned int first_page_address;
  unsigned int first_page_offset;
  /// Address of the page that will be read by next call to LoadBuffer()
  unsigned int next_page_address;
  /// Offset to the current record in page at next_page_address
  unsigned int next_page_offset;
  unsigned int current_page_address;
  unsigned int current_page_offset;
  unsigned int next_ref;
}; 

extern HomeFlash flash;
#endif

