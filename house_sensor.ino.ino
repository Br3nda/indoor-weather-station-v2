
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
      // if bit 7 of byte 0 is 0 then bits 6:4 are a signed diff of humidity from previous value, 2:0 are a signed diff of temp from previous value, bit 3 
      //      says that a following pressure value is unchanged and omitted
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

#define COUNT 60            // how many samples before trying to push upstream
#define DELAY 1000000       // 1 SEC in uS
#define MAGIC 0x68          // increment this (mod 256) when you make changes to force initialisation

extern "C" {
  #include "user_interface.h"
  extern struct rst_info resetInfo; 
  void esp_yield();
}

#include "Wire.h"
#include "HTS221.h"
#include "LPS25H.h"
#include "PC8563.h"
#include "CaptiveConfig.h"
#include "decompress.h"
#include "house_eeprom.h"
#include "Flash.h"



#ifndef cbi
#define cbi(sfr, bit) (_SFR_BYTE(sfr) &= ~_BV(bit))
#endif

#ifndef sbi
#define sbi(sfr, bit) (_SFR_BYTE(sfr) |= _BV(bit))
#endif

#include "pins_arduino.h"
#include "twi.h"


typedef struct rtc_info {
    unsigned char magic;    // MAGIC number
    unsigned char count;    // how many times to wait
    unsigned char state;    
#define STATE_HUMID_PRESENT     0x01    // we have a humididty sensor
#define STATE_PRESSURE_PRESENT  0x02    // we have a pressure sensor
#define STATE_RTC_PRESENT       0x04    // we have an external RTC
#define STATE_SENSORS_ACTIVE    0x08    // take a sample when you wake up
#define STATE_TIME_SET          0x10    // RTC time is valid

    unsigned char   compressor_state;   //
#define CSTATE_SAME       0x01          // we have an active 'same' entry
#define CSTATE_LAST_SAME  0x02          // the last entry we put was deltas '0'
#define CSTATE_NOPR       0x04          // the last entry had a skipped - 0 pressure valoue
    unsigned long   delay;    // how long to wait for 
    unsigned short  last_pressure;      // 0xffff means no last value
    signed char     last_temp;          // 0x7f means no last value
    unsigned char   last_humidity;      // 0xff means no last value
    unsigned char   _h0_rH, _h1_rH;     // humidity calibration parameters
    signed short    _H0_T0, _H1_T0;
    unsigned short  _T0_degC, _T1_degC; // temp calibration parameters
    signed short    _T0_OUT, _T1_OUT;

    unsigned char   boff; // offset into buffer for next sample
} rtc_info;

rtc_info save_info;
  
#define RTC_BUFF_BASE (sizeof(rtc_info))
#define RTC_BUFF_SIZE 256

#define HTS221_ADDRESS     0x5F
#define LPS25H_ADDRESS     0x5C

/// A CaptiveConfig is allocated in setup() if we want a config access point.
CaptiveConfig *configGetter(nullptr);

/// Used to confirm that device knows user wants to do something
uint8_t blinkCount(0);

/// Blinking is handled in loop(), undone by enter_deep_sleep()
void startBlink()
{
    pinMode(2, OUTPUT);
    blinkCount = 0x80;
}

// Unsure why, but Arduino precompiler thingy wants to put enter_deep_sleep()
// before rtc_mem_write...
void rtc_mem_write(int offset, void *p,  int bytes);

/// Call this, then return from setup() or loop() to enter deep sleep
void enter_deep_sleep()
{
  // Ensure that the notification LED is off before we go to sleep
  blinkCount = 0;
  digitalWrite(2, 1);
  eeprom.flush();

  rtc_mem_write(0, &save_info, sizeof(save_info));
  system_deep_sleep_set_option(0);
  system_deep_sleep(save_info.delay);
  esp_yield();
}

void
rtc_mem_write(int offset, void *p,  int bytes)
{
  union {
      unsigned char b[4];
      unsigned long l;
  }b;
  unsigned char *pp = (unsigned char *)p;
  volatile uint32 *rtc = ((uint32*)0x60001100) + 64 + (offset>>2);

  offset &= 3;
  if (offset) {
    b.l = *rtc;

    for (int i = offset; i < 4 && bytes>0; i++) {
        b.b[i] = *pp++;
        bytes--;
    }
    *rtc++ = b.l;
    if (bytes == 0)
      return;
  }
  if (((int)pp)&3) {
    while (bytes >= 4) {
      b.b[0] = pp[0];
      b.b[1] = pp[1];
      b.b[2] = pp[2];
      b.b[3] = pp[3];
      *rtc++ = b.l;
      pp += 4;
      bytes -= 4;
    }
  } else {
    while (bytes >= 4) {
      *rtc++ = *(unsigned long *)pp;
      pp += 4;
      bytes -= 4;
    }
  }
  if (bytes&3) {
    b.l = *rtc;
    for (int i = 0; i < (bytes&3); i++)
      b.b[i] = *pp++;
    *rtc = b.l;
  }
}


void
rtc_mem_read(int offset, void *p,  int bytes)
{
  union {
      unsigned char b[4];
      unsigned long l;
  }b;
  unsigned char *pp = (unsigned char *)p;
  volatile uint32 *rtc = ((uint32*)0x60001100) + 64 + (offset>>2);

  offset &= 3;
  if (offset) {
    b.l = *rtc;
    for (int i = offset; i < 4 && bytes>0; i++) {
        *pp++ = b.b[i];
        bytes--;
    }
    if (bytes == 0)
      return;
    rtc++;
  }
  
  if (((int)pp)&3) {
    while (bytes >= 4) {
      b.l = *rtc++;
      pp[0] = b.b[0];
      pp[1] = b.b[1];
      pp[2] = b.b[2];
      pp[3] = b.b[3];  
      pp += 4;
      bytes -= 4;
    }
  } else {
    while (bytes >= 4) {
      *(unsigned long *)pp = *rtc++;
      pp += 4;
      bytes -= 4;
    }
  }
  if (bytes&3) {
    b.l = *rtc;
    for (int i = 0; i < (bytes&3); i++)
      *pp++ = b.b[i];
  }
}

unsigned char 
readRegister(unsigned char addr, unsigned char reg)
{
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)addr, (uint8_t)1);
    while(!Wire.available());
    return Wire.read();  
}

void
log_string(const char *s)
{
  unsigned char c = 0xf5; 
  save_info.compressor_state = 0;
  rtc_mem_write(RTC_BUFF_BASE+save_info.boff++, &c, 1);
  for (;;) {
    rtc_mem_write(RTC_BUFF_BASE+save_info.boff++, (unsigned char *)s, 1);
    if (!*s)
      break;
    s++;
  }
}
void 
writeRegister(unsigned char addr, unsigned char reg, unsigned char val)
{
    Wire.beginTransmission(addr);
    if (!Wire.write(reg))
        return;
    if (!Wire.write(val)) 
        return;
    Wire.endTransmission();
}


void 
unload_rtc_buffer(int sz)
{
     save_info.last_humidity = 255;
     save_info.last_temp = 127;
     save_info.last_pressure = 0;
     save_info.compressor_state &= ~(CSTATE_SAME|CSTATE_LAST_SAME|CSTATE_NOPR);
     save_info.boff = 0;
}

void
write_time_signature()
{
    unsigned char b[6];

    save_info.compressor_state = 0;
    if (save_info.boff > (RTC_BUFF_SIZE-6-(save_info.delay==60000000?0:3))) {  // room for another?
      unload_rtc_buffer(save_info.boff);
    }
    b[0] = 0xf0 | (save_info.state&STATE_HUMID_PRESENT?1:0) | (save_info.state&STATE_PRESSURE_PRESENT?2:0);
    if ((save_info.state&(STATE_RTC_PRESENT|STATE_TIME_SET)) == (STATE_RTC_PRESENT|STATE_TIME_SET)) {
      pc_time tm;

      memset(&tm, 0, sizeof(tm));
      PC8563_RTC.read(tm);
      b[1] = tm.year-2000;
      b[2] = (tm.month<<4) | (tm.day>>1);
      b[3] = (tm.day<<7) | (tm.hour<<2) | (tm.minute>>4);
      b[4] = (tm.minute<<4) | 0xf;
      rtc_mem_write(RTC_BUFF_BASE+save_info.boff, &b[0], 5); // save the data
      save_info.boff += 5;
    } else {
      b[1] = 0xff;
      rtc_mem_write(RTC_BUFF_BASE+save_info.boff, &b[0], 2); // save the data
      save_info.boff += 2;
    }
    if (DELAY!=60000000) { // not 1 minute? output sampling rate
      int v = DELAY/1000000;
      b[0] = 0xf4;
      b[1] = v>>8;
      b[2] = v;
      rtc_mem_write(RTC_BUFF_BASE+save_info.boff, &b[0], 3); // save the data
      save_info.boff += 3;
    }
    if (save_info.boff > (RTC_BUFF_SIZE-4))   // room for another?
      unload_rtc_buffer(save_info.boff);
}

int 
get_compressed_byte(int offset) // called when dumping rtc contents
{
  unsigned char c;
  
  if (offset >= save_info.boff)
    return -1;
  rtc_mem_read(RTC_BUFF_BASE+offset, &c, 1);
  return c;
}

bool
XinitVariant() 
{
  // WARNING - crappy stack hacking occurs at the end of this routine if you add
  //    varibles here you must update the asm at the end of this routine
  //    
  unsigned long tm;
  unsigned char th_delta;
  unsigned char p_delta;
  signed char temp;
  unsigned char humidity;
  unsigned char status;
  bool force;
  bool same;
  int v;
  int dt, dh;
  int sz;
  unsigned char b[4];
  unsigned short adc;
 // end WARNING
 
 if (resetInfo.reason != REASON_DEEP_SLEEP_AWAKE)  // power on go set stuff up
    return 0;

  // ADC:
  //    1024 - nothing pressed
  //    369   - top button pressed    - upload data
  //    677   - bottom button pressed - insert mark in data
  //    265   - both buttons pressed  - go into setup mode
  //    
 adc = system_adc_read();
 
// Serial.print("adc=");  
// Serial.println(adc);

  rtc_mem_read(0, &save_info, sizeof(save_info));  
//  Serial.println("");  
//  Serial.print("state=");  
//  Serial.println(save_info.state,HEX);
  if (save_info.magic != MAGIC)
    return 0;
  if (save_info.state&STATE_SENSORS_ACTIVE) {
    if (adc > 500 && adc < 900) { // insert mark
      b[0] = 0xf6; // mark
      b[1] = 0x0;  // default mark
      rtc_mem_write(RTC_BUFF_BASE+save_info.boff, &b[0], 2); // save the data
      save_info.boff += 2;
      if (save_info.boff > (RTC_BUFF_SIZE-4)) {  // room for another?
       unload_rtc_buffer(save_info.boff);
      }
    }
    force=0;
    same=0;
    // activate internal pullups for twi.
    Wire.begin(4, 5);
    
    if (save_info.state&STATE_HUMID_PRESENT) 
      writeRegister(HTS221_ADDRESS, 0x20, 0x80|3); // wake up the HTS221
    if (save_info.state&STATE_PRESSURE_PRESENT) 
      writeRegister(LPS25H_ADDRESS, 0x20, 0x80|0x40); // wake up the LP25
    if (save_info.state&STATE_HUMID_PRESENT) {
      for (;;) {
        status = readRegister(HTS221_ADDRESS, 0x27);
        if (status&0x02) // humidity ready
            break;
      }
      v = readRegister(HTS221_ADDRESS, 0x29)<<8;
      v |= readRegister(HTS221_ADDRESS, 0x28);
//Serial.print("v=");
//Serial.println(v);
      if (v&0x8000)
        v |= 0xffff0000;

      v = (save_info._h0_rH + (((v-save_info._H0_T0)*(save_info._h1_rH-save_info._h0_rH))/(save_info._H1_T0-save_info._H0_T0)))>>1;
      if (v < 0) v = 0; else
      if (v > 100) v = 100;
      humidity = v;
//Serial.print("humidity=");Serial.println(humidity);
      for (;;) {
        if (status&0x01) // temp ready
            break;
        status = readRegister(HTS221_ADDRESS, 0x27);
      }
      v = readRegister(HTS221_ADDRESS, 0x2b)<<8;
      v |= readRegister(HTS221_ADDRESS, 0x2a);
      writeRegister(HTS221_ADDRESS, 0x20, 0);
      v = ((save_info._T0_degC + (((int16_t)v-(int16_t)save_info._T0_OUT)*(save_info._T1_degC-save_info._T0_degC))/((int16_t)save_info._T1_OUT-(int16_t)save_info._T0_OUT))+3)>>3;
      temp = v;
//Serial.print("temp=");Serial.println(temp);
      dt = temp-save_info.last_temp;
      save_info.last_temp = temp;
      dh = humidity-save_info.last_humidity;
      save_info.last_humidity = humidity;
      if (dh > 3 || dh < -4 || dt > 3 || dt < -4)
          force = 1;
//Serial.print("dh=");Serial.println(dh);
//Serial.print("dt=");Serial.println(dt);
      th_delta = (dt&0x7)|((dh&0x7)<<4);
    } else {
      th_delta = 0x00;
    }
    if (save_info.state&STATE_PRESSURE_PRESENT) {
      while (!(readRegister(LPS25H_ADDRESS, 0x27)&0x02))
        ;             
      v = readRegister(LPS25H_ADDRESS, 0x2a)<<4;  // MSB
      v |=  readRegister(LPS25H_ADDRESS, 0x29)>>4; //LSB
      writeRegister(LPS25H_ADDRESS, 0x20, 0x10);
      dt = v-save_info.last_pressure;
//Serial.print("press=");
//Serial.println(v);
      save_info.last_pressure = v;
      if (dt > 63 || dt < -64)
        force = 1;
// Serial.print("dt=");
//Serial.println(dt&0xff,HEX);
      p_delta = (dt&0x7f);
    } else {
      p_delta = 0;
    }
    sz=0;
//Serial.print(th_delta,HEX);
// Serial.print("--");
//Serial.println(p_delta,HEX);
    if (force) {    // force means send full values rather than del;tas
      save_info.compressor_state &= ~(CSTATE_SAME|CSTATE_LAST_SAME|CSTATE_NOPR);
      if (save_info.state&STATE_HUMID_PRESENT) {
        b[0] = 0x80|save_info.last_humidity;
        b[1] = (unsigned char)save_info.last_temp;
        sz = 2;
      } else {
        sz = 0;
      }
      if (save_info.state&STATE_PRESSURE_PRESENT) {
        b[sz] = 0x80|(save_info.last_pressure>>8);
        b[sz+1] = save_info.last_pressure;
        sz += 2;
      }
    } else {
      if (th_delta == 0x00 && p_delta == 0x00) {

// Serial.print("cstate=");
//Serial.println(save_info.compressor_state,HEX);        
         if (save_info.compressor_state&CSTATE_SAME) { // 3rd and subsequent deltas
            rtc_mem_read(RTC_BUFF_BASE+save_info.boff-1, &status, 1);
            if (status == 255) {   // repeat is full, add an extra one
                sz = 2;
                b[0] = 0xf8;
                b[1] = 1;
            } else {          // increment count
                save_info.boff--;
                b[0] = status+1;
                sz = 1;
            }
        } else
        if (save_info.compressor_state&CSTATE_LAST_SAME) { // 2nd delta convert previous delta into a 'repeat'
            if (save_info.state&STATE_HUMID_PRESENT) 
              save_info.boff--;
            if (save_info.state&STATE_PRESSURE_PRESENT && !(save_info.compressor_state&CSTATE_NOPR)) 
              save_info.boff--;
            save_info.compressor_state &= ~(CSTATE_LAST_SAME|CSTATE_NOPR);
            save_info.compressor_state |= CSTATE_SAME;
            sz = 2;
            b[0] = 0xf8;
            b[1] = 2;
        } else { // first '0' delta just store the '0'
          save_info.compressor_state |= CSTATE_LAST_SAME;          
          sz = 0;
          if (save_info.state&STATE_HUMID_PRESENT) {
            b[sz++] = th_delta|0x08;
            save_info.compressor_state |= CSTATE_NOPR;
          } else
          if (save_info.state&STATE_PRESSURE_PRESENT)
            b[sz++] = p_delta;
        }
      } else {  // non-0 delta just save the deltas
        save_info.compressor_state &= ~(CSTATE_SAME|CSTATE_LAST_SAME);
        sz = 0;
        if (p_delta == 0x00 && (save_info.state&(STATE_HUMID_PRESENT|STATE_PRESSURE_PRESENT)) == (STATE_HUMID_PRESENT|STATE_PRESSURE_PRESENT)) {
          save_info.compressor_state |= CSTATE_NOPR;
          b[sz++] = th_delta|0x08; 
        } else {
          save_info.compressor_state &= ~CSTATE_NOPR;
          if (save_info.state&STATE_HUMID_PRESENT)
            b[sz++] = th_delta;
          if (save_info.state&STATE_PRESSURE_PRESENT)
            b[sz++] = p_delta;
        }
      }
    }

    rtc_mem_write(RTC_BUFF_BASE+save_info.boff, &b[0], sz); // save the data
    save_info.boff += sz;
    if (save_info.boff > (RTC_BUFF_SIZE-4)) {  // room for another?
      unload_rtc_buffer(save_info.boff);
    }
  }
  save_info.count--;
  rtc_mem_write(0, &save_info, sizeof(save_info));
  if (!save_info.count)
      return 0;
  // ADC:
  //    1024 - nothing pressed
  //    369   - top button pressed    - upload data
  //    677   - bottom button pressed - insert mark in data
  //    265   - both buttons pressed  - go into setup mode
  //    
  if (adc < 500)  // if 'upload data' or 'go into setup mode' got into setup
    return 0;
  system_deep_sleep_set_option(0);
  system_deep_sleep(save_info.delay);
return 1;
  // unwind this stack frame and fake a return from user_init
  asm("l32i.n a0,a1,12+16;addi a1,a1,16+16;ret.n"); 
  // change '64' to match how much space gets allocated for this routine - '16' for the stack frame  
  // to find out the value run objdump on the resulting elf file, find the second line of initVariant()
  // it will be something like "addi    a1, a1, -48" take the '48' or whatever from here and replace it in the asm above (twice)
}

void 
log_time(time_stamp *t) 
{
  if (t->valid) {
    Serial.print("Date: ");
    Serial.print(t->day);
    Serial.print("/");
    Serial.print(t->month);
    Serial.print("/");
    Serial.print(t->year);
    Serial.print(" ");
    Serial.print(t->hour);
    Serial.print(":");
    Serial.print(t->minute);
    Serial.print(":");
    Serial.print(t->second);
    Serial.print(" ");
  }
}

void log_mark(time_stamp *t, int mark)
{
  log_time(t);
  Serial.print("mark ");
  Serial.println(mark);
}

void log_data(time_stamp *t, unsigned char valid_th, int temp, int humidity, unsigned char valid_p, int pressure)
{
  log_time(t);
  if (valid_th) {
    Serial.print(temp);
    Serial.print(" ");
    Serial.print(humidity);
    if (valid_p)
      Serial.print(" ");
  }
  if (valid_p) 
    Serial.print(pressure);
  Serial.println();
}

void
write_time(int year,int month, int day, int hour, int minute, int second)
{
  if (save_info.state&STATE_RTC_PRESENT) {
    pc_time tm;

    tm.second = second;
    tm.minute = minute;
    tm.hour = hour;
    tm.day = day;
    tm.month = month;
    tm.year = year;
    save_info.state |= STATE_TIME_SET;
    PC8563_RTC.write(tm);
    write_time_signature();
  }
}

void 
setup() {
  unsigned char v;

  Serial.begin(115200);
  Serial.println("");
  if (XinitVariant()) return;
  // put your setup code here, to run once:
 // Serial.begin(115200);
  Serial.println("");  
  Serial.print(resetInfo.reason);  
  Serial.print(" ");  
  rtc_mem_read(0, &save_info, sizeof(save_info));
  Serial.print(save_info.count);  
  Serial.println(" begin"); 
  if (resetInfo.reason != REASON_DEEP_SLEEP_AWAKE || save_info.magic != MAGIC) {  // system powerup reset
    bool humidityPresent, pressurePresent;

    memset(&save_info, 0, sizeof(save_info));
    save_info.magic = MAGIC;
    Serial.println("VCW sensor");
    Wire.begin(4, 5);
    Serial.println("start humidity");
    humidityPresent = smeHumidity.begin();
    if (!humidityPresent)  {
        Serial.println("- NO HT221 Temperature/Humidity Sensor found");
    } else {
       smeHumidity.deactivate();
       save_info.state |= STATE_HUMID_PRESENT;
       save_info._h0_rH = smeHumidity._h0_rH;     // humidity calibration parameters
       save_info._h1_rH = smeHumidity._h1_rH;     
       save_info._H0_T0 = smeHumidity._H0_T0;
       save_info._H1_T0 = smeHumidity._H1_T0;
       save_info._T0_degC = smeHumidity._T0_degC; // temp calibration parameters
       save_info._T1_degC = smeHumidity._T1_degC; // temp calibration parameters
       save_info._T0_OUT = smeHumidity._T0_OUT;
       save_info._T1_OUT = smeHumidity._T1_OUT;
    }
    save_info.last_humidity = 255;
    save_info.last_temp = 127;
    Serial.println("start pressure"); 
    pressurePresent = smePressure.begin();
    if (!pressurePresent) {
        Serial.println("- NO LPS25 Pressure Sensor found");
    } else {
      save_info.state |= STATE_PRESSURE_PRESENT;
      smePressure.deactivate();
    }
    save_info.last_pressure = 0;
    if (!PC8563_RTC.begin()) {
      Serial.println("- NO PC8563 RTC found");
    } else {
      save_info.state |= STATE_RTC_PRESENT; 
    }
    if (save_info.state&(STATE_PRESSURE_PRESENT|STATE_HUMID_PRESENT))
      save_info.state |= STATE_SENSORS_ACTIVE;


    save_info.state |= STATE_TIME_SET|STATE_RTC_PRESENT; // remove this when we have a working time load thing
    write_time(2016, 1, 1, 0, 0, 0);
    save_info.delay = DELAY;   // 1 sec
    save_info.count = COUNT;
  } else {
#ifdef NOTDEF
    for (int i = 0; i < save_info.boff; i++){
      unsigned char c;
      rtc_mem_read(RTC_BUFF_BASE+i, &c, 1);
      Serial.print(c, HEX);
      Serial.print(" ");
    }
    Serial.println(" ");
#endif
    int samples = dump_rtc_data();
     Serial.print(samples);
     Serial.print(" samples in ");
     Serial.print(save_info.boff);
     Serial.println("bytes");
     
     save_info.boff = 0;
     save_info.last_humidity = 255;
     save_info.last_temp = 127;
     save_info.last_pressure = 0;
     write_time_signature();
  }
  if (save_info.count == 0)
    save_info.count = COUNT;
#ifdef NOTDEF
smeHumidity.begin();
smePressure.begin();
delay(8);
Serial.print("TEMP=");
Serial.println(smeHumidity.readTemperature());
Serial.print("HUM=");
Serial.println(smeHumidity.readHumidity());
Serial.print("PRESS=");
Serial.println(smePressure.readPressure());

smeHumidity.deactivate();
smePressure.deactivate();
#endif
  //
  //  do any setup() stuff here
  //
  //---------------------------------------------------
  //(void)eeprom.get_pointer();  // force a load for testing
   // ADC:
  //    1024 - nothing pressed
  //    369   - top button pressed    - upload data
  //    677   - bottom button pressed - insert mark in data
  //    265   - both buttons pressed  - go into setup mode
  //    
  {
      unsigned short adc = system_adc_read();
      if (adc < 500) { // ignore other cases
        if (adc < 300) {
          // Both buttons pressed
          Serial.println("Starting captive config.");
          startBlink();
          configGetter = new CaptiveConfig();

          return; // This return without enter_deep_sleep() means "go to loop()"
        } else {
          Serial.println("Want to upload data.");

          // TODO: Get WiFi credentials from flash, connect, upload data, etc.
        }
      }
  }

  enter_deep_sleep();
}

void 
loop() {
    if(blinkCount) {
        digitalWrite(2, !(--blinkCount & 0x10) );
    }

    if( configGetter ) {
        if( configGetter->haveConfig() ) {
            Serial.println("Got config");

            // TODO: Do something useful with the configuration

            delete configGetter;
            configGetter = nullptr;
            enter_deep_sleep();
        } else {
            delay(15);
        }
    }
}
