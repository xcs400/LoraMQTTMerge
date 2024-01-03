/*  
  OpenMQTTGateway Addon  - ESP8266 or Arduino program for home automation 
 
   Act as a wifi or ethernet gateway between your 433mhz/infrared IR signal  and a MQTT broker
   Send and receiving command by MQTT
   
   This is the Climate Addon:
   - Measures Temperature, Humidity and Pressure
   - Generates Values for: Temperature in degrees C and F, Humidity in %, Pressure in Pa, Altitude in Meter and Feet
   - Required Hardware Module: HTU21
   - Required Library: SparkFun HTU21 Library v1.1.3

   Connection Schemata:
   --------------------

   HTU21 ------> Arduino Uno ----------> ESP8266
   ==============================================
   Vcc ---------> 5V -------------------> Vu (5V)
   GND ---------> GND ------------------> GND
   SCL ---------> Pin A5 ---------------> D1
   SDA ---------> Pin A4 ---------------> D2
   
    This file is part of OpenMQTTGateway.
    
    OpenMQTTGateway is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    OpenMQTTGateway is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "User_config.h"

#ifdef ZsensorHTU21
#  include <stdint.h>

#  include "SparkFunHTU21D.h"
#  include "Wire.h" // Library for communication with I2C / TWI devices
#  include "config_HTU21.h"

//Time used to wait for an interval before resending measured values
unsigned long timehtu21 = 0;
int counterror = 0;

//Global sensor object
HTU21D htuSensor;

int setupZsensorHTU21() {
  delay(10); // Gives the Sensor enough time to turn on
  Log.notice(F("HTU21 Initialized - begin()" CR));

#  if defined(ESP32)
  Wire.begin(I2C_SDA, I2C_SCL);
  htuSensor.begin(Wire);

  float HtuTempC = htuSensor.readTemperature();
  float HtuHum = htuSensor.readHumidity();

  if (HtuTempC >= 998 || HtuHum >= 998) {
    Log.error(F("Failed to read from sensor HTU21! ignore" CR));
    counterror = 1;
    return 0;
  }

#  else
  htuSensor.begin();
#  endif

  return 1;
}

void MeasureTempHum() {
  if (millis() > (timehtu21 + TimeBetweenReadinghtu21)) {
    Log.trace(F("Read HTU21 Sensor" CR));

    timehtu21 = millis();
    static float persisted_htu_tempc;
    static float persisted_htu_hum;

    float HtuTempC;
    float HtuHum;

    if (counterror == 0) {
      HtuTempC = htuSensor.readTemperature();
      HtuHum = htuSensor.readHumidity();

      if (HtuTempC >= 998 || HtuHum >= 998) {
        Log.error(F("Failed to read from sensor HTU21!" CR));
        return;
      }

    } else
      return;

    // Check if reads failed and exit early (to try again).
    if (isnan(HtuTempC) || isnan(HtuHum)) {
      Log.error(F("Failed to read from sensor HTU21!" CR));
    } else {
      Log.notice(F("Creating HTU21 buffer" CR));
      StaticJsonDocument<JSON_MSG_BUFFER> HTU21dataBuffer;
      JsonObject HTU21data = HTU21dataBuffer.to<JsonObject>();
      // Generate Temperature in degrees C
      if (HtuTempC != persisted_htu_tempc || htu21_always) {
        float HtuTempF = (HtuTempC * 1.8) + 32;
        HTU21data["tempc"] = (float)HtuTempC;
        HTU21data["tempf"] = (float)HtuTempF;
        HTU21data["id"] = "Yaourt2";
        HTU21data["TempCelsius"] = (float)HtuTempC;
        ;
        HTU21data["Vbatt"] = (float)HtuHum;

      } else {
        Log.notice(F("Same Temp. Don't send it" CR));
      }

      // Generate Humidity in percent
      if (HtuHum != persisted_htu_hum || htu21_always) {
        HTU21data["hum"] = (float)HtuHum;
      } else {
        Log.notice(F("Same Humidity. Don't send it" CR));
      }

      char out[200];
      struct tm timeinfo;
      if (!getLocalTime(&timeinfo)) {
        Serial.println("Failed to obtain time");
      }
      sprintf(out, "%d-%.2d-%.2d/%.2d-%.2d-%.2d", timeinfo.tm_mday, timeinfo.tm_mon + 1, 1900 + timeinfo.tm_year, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
      Serial.println(out);

      HTU21data["Time"] = String(out);
      HTU21data["name"] = HTU21data["id"];
      HTU21data["id"] = String("/Yaourt2") + String("/History/") + String(out);

      //    String origin = String(HTUTOPIC) + "/Yaourt2/"  + out;
      String origin = String("/OneWiretoMQTT/ds1820") + "/Yaourt2/" + out;

      HTU21data["origin"] = origin; // HTUTOPIC;
      handleJsonEnqueue(HTU21data);
    }
    persisted_htu_tempc = HtuTempC;
    persisted_htu_hum = HtuHum;
  }
}

#endif
