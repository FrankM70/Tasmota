/*
  xsns_05_esp32_ds18x20.ino - DS18x20 temperature sensor support for ESP32 Tasmota

  Copyright (C) 2021  Heiko Krupp, Theo Arends and md5sum-as (https://github.com/md5sum-as)

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifdef ESP32
#ifdef USE_DS18x20
/*********************************************************************************************\
 * DS18B20 - Temperature - Multiple sensors
\*********************************************************************************************/

#define XSNS_05              5

//#define DS18x20_USE_ID_AS_NAME     // Use last 3 bytes for naming of sensors

/* #define DS18x20_USE_ID_ALIAS in my_user_config.h or user_config_override.h
  * Use alias for fixed sensor name in scripts by autoexec. Command: DS18Alias XXXXXXXXXXXXXXXX,N where XXXXXXXXXXXXXXXX full serial and N number 1-255
  * Result in JSON:  "DS18Alias_2":{"Id":"000003287CD8","Temperature":26.3} (example with N=2)
  * add 8 bytes used memory
*/

#define DS18S20_CHIPID       0x10  // +/-0.5C 9-bit
#define DS1822_CHIPID        0x22  // +/-2C 12-bit
#define DS18B20_CHIPID       0x28  // +/-0.5C 12-bit
#define MAX31850_CHIPID      0x3B  // +/-0.25C 14-bit

#define W1_SKIP_ROM          0xCC
#define W1_CONVERT_TEMP      0x44
#define W1_READ_SCRATCHPAD   0xBE

#ifndef DS18X20_MAX_SENSORS         // DS18X20_MAX_SENSORS fallback to 8 if not defined in user_config_override.h
#define DS18X20_MAX_SENSORS  8
#endif

const char kDs18x20Types[] PROGMEM = "DS18x20|DS18S20|DS1822|DS18B20|MAX31850";

uint8_t ds18x20_chipids[] = { 0, DS18S20_CHIPID, DS1822_CHIPID, DS18B20_CHIPID, MAX31850_CHIPID };

struct {
  float temp_sum;
  uint16_t numread;
  uint8_t address[8];
  uint8_t index;
  uint8_t valid;
  int8_t pins_id;
#ifdef DS18x20_USE_ID_ALIAS
  uint8_t alias;
#endif //DS18x20_USE_ID_ALIAS
} ds18x20_sensor[DS18X20_MAX_SENSORS];

#include <OneWire.h>
OneWire *ds = nullptr;
OneWire *ds18x20_gpios[MAX_DSB];

struct {
  char name[17];
  uint8_t sensors;
  uint8_t gpios;    // Count of GPIO found
} DS18X20Data;

/********************************************************************************************/

void Ds18x20Init(void) {
  DS18X20Data.gpios = 0;
  for (uint32_t pins = 0; pins < MAX_DSB; pins++) {
    if (PinUsed(GPIO_DSB, pins)) {
      ds18x20_gpios[pins] = new OneWire(Pin(GPIO_DSB, pins));
      DS18X20Data.gpios++;
    }
  }
  Ds18x20Search();
  AddLog(LOG_LEVEL_DEBUG, PSTR(D_LOG_DSB D_SENSORS_FOUND " %d"), DS18X20Data.sensors);
}

void Ds18x20Search(void) {
  uint8_t num_sensors = 0;
  uint8_t sensor = 0;

  for (uint8_t pins = 0; pins < DS18X20Data.gpios; pins++) {
    ds = ds18x20_gpios[pins];
    ds->reset_search();
    for (num_sensors; num_sensors < DS18X20_MAX_SENSORS; num_sensors) {
      if (!ds->search(ds18x20_sensor[num_sensors].address)) {
        ds->reset_search();
        break;
      }
      // If CRC Ok and Type DS18S20, DS1822, DS18B20 or MAX31850
      if ((OneWire::crc8(ds18x20_sensor[num_sensors].address, 7) == ds18x20_sensor[num_sensors].address[7]) &&
        ((ds18x20_sensor[num_sensors].address[0] == DS18S20_CHIPID) ||
          (ds18x20_sensor[num_sensors].address[0] == DS1822_CHIPID) ||
          (ds18x20_sensor[num_sensors].address[0] == DS18B20_CHIPID) ||
          (ds18x20_sensor[num_sensors].address[0] == MAX31850_CHIPID))) {
#ifdef DS18x20_USE_ID_ALIAS
        ds18x20_sensor[num_sensors].alias=0;
#endif
        ds18x20_sensor[num_sensors].pins_id = pins;
        num_sensors++;
      }
    }
  }

  for (uint32_t i = 0; i < num_sensors; i++) {
    ds18x20_sensor[i].index = i;
  }
  for (uint32_t i = 0; i < num_sensors; i++) {
    for (uint32_t j = i + 1; j < num_sensors; j++) {
      if (uint32_t(ds18x20_sensor[ds18x20_sensor[i].index].address) > uint32_t(ds18x20_sensor[ds18x20_sensor[j].index].address)) {
        std::swap(ds18x20_sensor[i].index, ds18x20_sensor[j].index);
      }
    }
  }
  DS18X20Data.sensors = num_sensors;
}

void Ds18x20Convert(void) {
  for (uint32_t i = 0; i < DS18X20Data.gpios; i++) {
    ds = ds18x20_gpios[i];
    ds->reset();
    ds->write(W1_SKIP_ROM);        // Address all Sensors on Bus
    ds->write(W1_CONVERT_TEMP);    // start conversion, no parasite power on at the end
//    delay(750);                   // 750ms should be enough for 12bit conv
  }
}

bool Ds18x20Read(uint8_t sensor, float &t) {
  uint8_t data[12];
  int8_t sign = 1;

  t = NAN;

  uint8_t index = ds18x20_sensor[sensor].index;
  if (ds18x20_sensor[index].valid) { ds18x20_sensor[index].valid--; }
  ds = ds18x20_gpios[ds18x20_sensor[index].pins_id];
  ds->reset();
  ds->select(ds18x20_sensor[index].address);
  ds->write(W1_READ_SCRATCHPAD); // Read Scratchpad

  for (uint32_t i = 0; i < 9; i++) {
    data[i] = ds->read();
  }
  if (OneWire::crc8(data, 8) == data[8]) {
    switch(ds18x20_sensor[index].address[0]) {
      case DS18S20_CHIPID: {
        int16_t tempS = (((data[1] << 8) | (data[0] & 0xFE)) << 3) | ((0x10 - data[6]) & 0x0F);
        t = ConvertTemp(tempS * 0.0625f - 0.250f);
        ds18x20_sensor[index].valid = SENSOR_MAX_MISS;
        return true;
      }
      case DS1822_CHIPID:
      case DS18B20_CHIPID: {
        uint16_t temp12 = (data[1] << 8) + data[0];
        if (temp12 > 2047) {
          temp12 = (~temp12) +1;
          sign = -1;
        }
        t = ConvertTemp(sign * temp12 * 0.0625f);  // Divide by 16
        ds18x20_sensor[index].valid = SENSOR_MAX_MISS;
        return true;
      }
      case MAX31850_CHIPID: {
        int16_t temp14 = (data[1] << 8) + (data[0] & 0xFC);
        t = ConvertTemp(temp14 * 0.0625f);  // Divide by 16
        ds18x20_sensor[index].valid = SENSOR_MAX_MISS;
        return true;
      }
    }
  }
  AddLog(LOG_LEVEL_DEBUG, PSTR(D_LOG_DSB D_SENSOR_CRC_ERROR));
  return false;
}

void Ds18x20Name(uint8_t sensor) {
  uint8_t index = sizeof(ds18x20_chipids);
  while (--index) {
    if (ds18x20_sensor[ds18x20_sensor[sensor].index].address[0] == ds18x20_chipids[index]) {
      break;
    }
  }
  GetTextIndexed(DS18X20Data.name, sizeof(DS18X20Data.name), index, kDs18x20Types);
  if (DS18X20Data.sensors > 1) {
#ifdef DS18x20_USE_ID_AS_NAME
    char address[17];
    for (uint32_t j = 0; j < 3; j++) {
      sprintf(address+2*j, "%02X", ds18x20_sensor[ds18x20_sensor[sensor].index].address[3-j]);  // Only last 3 bytes
    }
    snprintf_P(DS18X20Data.name, sizeof(DS18X20Data.name), PSTR("%s%c%s"), DS18X20Data.name, IndexSeparator(), address);
#else
uint8_t print_ind = sensor +1;
#ifdef DS18x20_USE_ID_ALIAS
    if (ds18x20_sensor[sensor].alias) {
      snprintf_P(DS18X20Data.name, sizeof(DS18X20Data.name), PSTR("DS18Sens"));
      print_ind = ds18x20_sensor[sensor].alias;
    }
#endif
    snprintf_P(DS18X20Data.name, sizeof(DS18X20Data.name), PSTR("%s%c%d"), DS18X20Data.name, IndexSeparator(), print_ind);
#endif
  }
}

/********************************************************************************************/

void Ds18x20EverySecond(void) {
  if (!DS18X20Data.sensors) { return; }

  if (TasmotaGlobal.uptime & 1) {
    // 2mS
//    Ds18x20Search();      // Check for changes in sensors number
    Ds18x20Convert();     // Start Conversion, takes up to one second
  } else {
    float t;
    for (uint32_t i = 0; i < DS18X20Data.sensors; i++) {
      // 12mS per device
      if (Ds18x20Read(i, t)) {   // Read temperature
        if (Settings->flag5.ds18x20_mean) {
          if (ds18x20_sensor[i].numread++ == 0) {
            ds18x20_sensor[i].temp_sum = 0;
          }
          ds18x20_sensor[i].temp_sum += t;
        }
      } else {
        Ds18x20Name(i);
        AddLogMissed(DS18X20Data.name, ds18x20_sensor[ds18x20_sensor[i].index].valid);
      }
    }
  }
}

void Ds18x20Show(bool json) {
  float t;

  uint8_t dsxflg = 0;
  for (uint32_t i = 0; i < DS18X20Data.sensors; i++) {
    if (Ds18x20Read(i, t)) {           // Check if read failed
      Ds18x20Name(i);

      if (json) {
        if (Settings->flag5.ds18x20_mean) {
          if ((0 == TasmotaGlobal.tele_period) && ds18x20_sensor[i].numread) {
            t = ds18x20_sensor[i].temp_sum / ds18x20_sensor[i].numread;
            ds18x20_sensor[i].numread = 0;
          }
        }
        char address[17];
        for (uint32_t j = 0; j < 6; j++) {
          sprintf(address+2*j, "%02X", ds18x20_sensor[ds18x20_sensor[i].index].address[6-j]);  // Skip sensor type and crc
        }
        ResponseAppend_P(PSTR(",\"%s\":{\"" D_JSON_ID "\":\"%s\",\"" D_JSON_TEMPERATURE "\":%*_f}"),
          DS18X20Data.name, address, Settings->flag2.temperature_resolution, &t);
        dsxflg++;
#ifdef USE_DOMOTICZ
        if ((0 == TasmotaGlobal.tele_period) && (1 == dsxflg)) {
          DomoticzFloatSensor(DZ_TEMP, t);
        }
#endif  // USE_DOMOTICZ
#ifdef USE_KNX
        if ((0 == TasmotaGlobal.tele_period) && (1 == dsxflg)) {
          KnxSensor(KNX_TEMPERATURE, t);
        }
#endif  // USE_KNX
#ifdef USE_WEBSERVER
      } else {
        WSContentSend_Temp(DS18X20Data.name, t);
#endif  // USE_WEBSERVER
      }
    }
  }
}

#ifdef DS18x20_USE_ID_ALIAS
const char kds18Commands[] PROGMEM = "|"  // No prefix
  D_CMND_DS_ALIAS;

void (* const DSCommand[])(void) PROGMEM = {
  &CmndDSAlias };

void CmndDSAlias(void) {
  uint8_t tmp;
  uint8_t sensor=255;
  char argument[XdrvMailbox.data_len];
  char address[17];

  if (ArgC()==2) {
    tmp=atoi(ArgV(argument, 2));
    ArgV(argument,1);

    for (uint32_t i = 0; i < DS18X20Data.sensors; i++) {
      for (uint32_t j = 0; j < 8; j++) {
        sprintf(address+2*j, "%02X", ds18x20_sensor[i].address[7-j]);
      }
      if (!strncmp(argument,address,12)) {
        ds18x20_sensor[i].alias=tmp;
        break;
      }
    }
  }
  Response_P(PSTR("{"));
  for (uint32_t i = 0; i < DS18X20Data.sensors; i++) {
    Ds18x20Name(i);
    char address[17];
    for (uint32_t j = 0; j < 8; j++) {
      sprintf(address+2*j, "%02X", ds18x20_sensor[i].address[7-j]);  // Skip sensor type and crc
    }
    ResponseAppend_P(PSTR("\"%s\":{\"" D_JSON_ID "\":\"%s\"}"),DS18X20Data.name, address);
    if (i < DS18X20Data.sensors-1) {ResponseAppend_P(PSTR(","));}
  }
  ResponseAppend_P(PSTR("}"));
}
#endif // DS18x20_USE_ID_ALIAS

/*********************************************************************************************\
 * Interface
\*********************************************************************************************/

bool Xsns05(uint8_t function) {
  bool result = false;

  if (PinUsed(GPIO_DSB, GPIO_ANY)) {
    switch (function) {
      case FUNC_INIT:
        Ds18x20Init();
        break;
      case FUNC_EVERY_SECOND:
        Ds18x20EverySecond();
        break;
      case FUNC_JSON_APPEND:
        Ds18x20Show(1);
        break;
#ifdef USE_WEBSERVER
      case FUNC_WEB_SENSOR:
        Ds18x20Show(0);
        break;
#endif  // USE_WEBSERVER
#ifdef DS18x20_USE_ID_ALIAS
      case FUNC_COMMAND:
        result = DecodeCommand(kds18Commands, DSCommand);
        break;
#endif // DS18x20_USE_ID_ALIAS
    }
  }
  return result;
}

#endif  // USE_DS18x20
#endif  // ESP32
