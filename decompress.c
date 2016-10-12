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

#include "decompress.h"

static unsigned char dm[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

static void
increment_time(time_stamp *t, int period)
{
  int i, d;
  
  if (!t->valid)
    return;
    
  i = period%60;
  period /= 60;
  t->second += i;
  if (t->second >= 60) {
      t->second -= 60;
      period++;
  } else
  if (period == 0)
    return;
  
  i = period%60;
  period /= 60;
  t->minute += i;
  if (t->minute >= 60) {
      t->minute -= 60;
      period++;
  } else
  if (period == 0)
    return;
  
  i = period%24;
  period /= 24;
  t->hour += i;
  if (t->hour >= 24) {
      t->hour -= 24;
      period++;
  } else
  if (period == 0)
    return;
  d = dm[t->month];
  if (t->month == 2 && (t->year&3) == 0)
    d++;
  period += t->day;
  if (period > d) {
      t->day = period-d+1;
      t->month++;
      if (t->month > 12) {
          t->month = 1;
          t->year++;
      }
  } else  {
      t->day = period;
  }
}

int
dump_rtc_data(void)
{
  int i;
  unsigned char stream_type=0;
  int last_temp, last_pressure, last_humidity;
  unsigned char b[5];
  unsigned char c=0x66;
  int samples=0;
  unsigned char valid_th=0;
  unsigned char valid_p=0;
  time_stamp tm;
  int period = 60;

  tm.valid = 0;
  tm.second = 0;
  for (i = 0; ;) {
    c = get_compressed_byte(i);
//printf("C=%x\n",c);
    i++;
    if ((c&0xf0) == 0xf0) {
      if (c == 0xff)
          return samples;
      switch (c&0xf) {
      case 0:
      case 1:
      case 2:
      case 3:
        stream_type = c&0x3;
        valid_p = stream_type >= 2;
        valid_th = (stream_type&1) != 0;
        for (int j = 0; j < 5; j++)
            b[j] = get_compressed_byte(i+j);
        if (b[0]==0xff) {
            i++;
            tm.valid = 0;
            break;
        }
        i += ((b[3]&0xf)==0xf?4:5);
        tm.valid = 1;
        tm.year = b[0]+2000;
        tm.month = b[1]>>4;
        tm.day = ((b[1]&0xf)<<1)|((b[2]>>7)&1);
        tm.hour = (b[2]>>2)&0x1f;
        tm.minute = ((b[2]&3)<<4)|(b[3]>>4);
        if ((b[3]&0xf)!=0xf) 
           tm.second = b[4]&0x3f;
        break;
      case 4:
        b[0] = get_compressed_byte(i);
        b[1] =  get_compressed_byte(i+1);
        i += 2;
//        Serial.print("Data rate: ");
//        Serial.print((b[0]<<8)|b[1]);
//        Serial.println(" seconds/sample");
        period = (b[0]<<8)|b[1];
        break;
      case 5:
#ifdef NOTDEF
        Serial.print("Comment: ");
#endif
        for (;;) {
            char ch;
            
            ch = get_compressed_byte(i);
            i++;
            if (ch == 0 || ch == 0xff)
              break;
#ifdef NOTDEF
            Serial.print(ch);
#endif
        }
#ifdef NOTDEF
        Serial.println("");
#endif
        break;
      case 6:
        b[0] = get_compressed_byte(i);
        i++;
        log_mark(&tm, b[0]);
        break;
      case 7: // null
        break;
      case 8:
        b[0] = get_compressed_byte(i);
        i++;
        if (stream_type == 0) {
            //Serial.println("No data type specified - quitting");
            return samples;
        }
        while (b[0]--) {
          samples++;
          log_data(&tm, valid_th,  last_temp, last_humidity, valid_p, last_pressure);
          increment_time(&tm, period);
        }
        break;
      default:
        //Serial.print("Invalid escape code - ");
        //Serial.println(c,HEX);
        return samples;
      }
    } else {
        if (stream_type == 0) {
            //Serial.println("No data type specified - quitting");
            return samples;
        }
        samples++;
        if (!(c&0x80)) { // delta?
          int skip=0;
          if (stream_type&1) {
            int d=c&0x7;
            if (d&0x4) // sign extend
              d -= 8;
            last_temp += d;
            d=(c>>4)&0x7;
            if (d&0x4)  // sign extend 
              d -= 8;
            last_humidity += d;
            skip= c&0x8;
            if (stream_type&2 && !skip)
              c = get_compressed_byte(i++);
          }
          if (stream_type&2 && !skip) {
            int d=c;
            if (d&0x40) // sign extend
              d -= 128;
            last_pressure += d;
          }
        } else {
          if (stream_type&1) {
            last_humidity = c&0x7f;
            b[0] = get_compressed_byte(i++);
            last_temp = b[0];
            if (last_temp&0x80) // sign extend
              last_temp -= 256;
            if (stream_type&2) 
              c = get_compressed_byte(i++);
          }
          if (stream_type&2) {
            b[1] = get_compressed_byte(i++);
            last_pressure = ((c&0x7f)<<8)|b[1];
          }
        }
        log_data(&tm, valid_th,  last_temp, last_humidity, valid_p, last_pressure);
        increment_time(&tm, period);
    }
  }
  return samples;
}

