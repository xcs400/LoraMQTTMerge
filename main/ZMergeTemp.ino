/*
  OpenMQTTGateway Addon  - ESP8266 or Arduino program for home automation

   Act as a wifi or ethernet gateway between your 433mhz/infrared IR signal  and a MQTT broker
   Send and receiving command by MQTT

    Supported boards with displays

    HELTEC ESP32 LORA - SSD1306 / Onboard 0.96-inch 128*64 dot matrix OLED display
    LILYGO® LoRa32 V2.1_1.6.1 433 Mhz / https://www.lilygo.cc/products/lora3?variant=42476923879605

    Copyright: (c)Florian ROBERT

    Contributors:
    - 1technophile
    - NorthernMan54

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
#define ZMergeTemp
#if defined(ZMergeTemp)

#  include <ArduinoJson.h>

#  include "ArduinoLog.h"
#  include "User_config.h"
#  include "config_MergeTemp.h"

/*
module setup, for use in Arduino setup
*/
// Returns number of days since civil 1970-01-01.  Negative values indicate
//    days prior to 1970-01-01.
// Preconditions:  y-m-d represents a date in the civil (Gregorian) calendar
//                 m is in [1, 12]
//                 d is in [1, last_day_of_month(y, m)]
//                 y is "approximately" in
//                   [numeric_limits<Int>::min()/366, numeric_limits<Int>::max()/366]
//                 Exact range of validity is:
//                 [civil_from_days(numeric_limits<Int>::min()),
//                  civil_from_days(numeric_limits<Int>::max()-719468)]
template <class Int>
Int days_from_civil(Int y, unsigned m, unsigned d) noexcept {
  static_assert(std::numeric_limits<unsigned>::digits >= 18,
                "This algorithm has not been ported to a 16 bit unsigned integer");
  static_assert(std::numeric_limits<Int>::digits >= 20,
                "This algorithm has not been ported to a 16 bit signed integer");
  y -= m <= 2;
  const Int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400); // [0, 399]
  const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1; // [0, 365]
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy; // [0, 146096]
  return era * 146097 + static_cast<Int>(doe) - 719468;
}

// Returns year/month/day triple in civil calendar
// Preconditions:  z is number of days since 1970-01-01 and is in the range:
//                   [numeric_limits<Int>::min(), numeric_limits<Int>::max()-719468].
template <class Int>

std::tuple<Int, unsigned, unsigned>
civil_from_days(Int z) noexcept {
  static_assert(std::numeric_limits<unsigned>::digits >= 18,
                "This algorithm has not been ported to a 16 bit unsigned integer");
  static_assert(std::numeric_limits<Int>::digits >= 20,
                "This algorithm has not been ported to a 16 bit signed integer");
  z += 719468;
  const Int era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097); // [0, 146096]
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; // [0, 399]
  const Int y = static_cast<Int>(yoe) + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100); // [0, 365]
  const unsigned mp = (5 * doy + 2) / 153; // [0, 11]
  const unsigned d = doy - (153 * mp + 2) / 5 + 1; // [1, 31]
  const unsigned m = mp < 10 ? mp + 3 : mp - 9; // [1, 12]
  return std::tuple<Int, unsigned, unsigned>(y + (m <= 2), m, d);
}

void erasebMQTTtopic(const char* topic, bool retainFlag);

void setupMergeTemp() {
  Log.notice(F("Setup MergeTemp" CR));
}

/*
module loop, for use in Arduino loop
*/

#  define NBITE 2000
int bigtime[NBITE];
int16_t bigtempv[NBITE];

int16_t bigvbat[NBITE];
//int16_t bigcharge[NBITE];

#  define NBITESHORT 48
int shorttime[NBITESHORT + 5];
int16_t shorttempv[NBITESHORT + 5];
int16_t maxtemp[NBITESHORT + 5];
int16_t mintemp[NBITESHORT + 5];

int16_t shortvbat[NBITESHORT + 5];
//int16_t shortcharge[NBITESHORT + 10];

int secondnow = 0xffffffff;
int pos = 0;
uint minpos = 0xffffffff;

StaticJsonDocument<1000> copyMERGETEMPdata;

//---------LoopMergeTemp---------------------------------------------------------------------------
StaticJsonDocument<6000> Histo;
char Tearray[2000];
char DateArray[4000];
char vbatarray[2000];
char minarray[2000];
char maxarray[2000];
//char chargeArray[4000];
int16_t Jour_tmmaxtemp, Jour_tmmintemp;

//-------------------------------------------
int stateid = 0;
int swichtid_signal = 0;

std::string extractId(const std::string& path) {
    size_t pos = path.find_last_of('/');
    if (pos != std::string::npos) {
        return path.substr(pos + 1);
    }
    return path; // Return the original string if no '/' is found
}
std::string findNomenclair(const std::string& id, const std::string& s1, const std::string& s2, const std::string& s3, const std::string& s4) {
    // Helper lambda to find and return the nomenclair from a string if the id matches
    auto getNomenclair = [&id](const std::string& s) -> std::string {
        size_t pos = s.find('=');
        if (pos != std::string::npos && s.substr(0, pos) == id) {
            return s.substr(pos + 1);
        }
        return "";
    };

    // Check each string
    std::string nomenclair = getNomenclair(s1);
    if (!nomenclair.empty()) return nomenclair;

    nomenclair = getNomenclair(s2);
    if (!nomenclair.empty()) return nomenclair;

    nomenclair = getNomenclair(s3);
    if (!nomenclair.empty()) return nomenclair;

    nomenclair = getNomenclair(s4);
    if (!nomenclair.empty()) return nomenclair;

    // If not found, return the id
    return id;
}



void fechtSubId(std::string id) {
  char topicH[mqtt_topic_max_size];
  strcpy(topicH, mqtt_topic);
  strcat(topicH, gateway_name);
  strcat(topicH, id.c_str() );  // /CLIMAtoMQTT/htu/yaourt1   /LORAtoMQTT  /OneWiretoMQTT/ds1820
  strcat(topicH, (char*)"/#");

  if (client.subscribe(topicH)) {
    Log.notice(F(" subscrib : %s" CR), topicH);
  }

   std::string idfin = extractId(id);

    std::string result = findNomenclair(idfin, s1, s2, s3, s4);
  

  std::string payload = "{id:\"" + id  + "\" ,  visiblename:\"" + result  + "\"  }"; // ajouyte sensor name
 std::string  sensorname =  strrchr( id.c_str(), '/' ) +1 ;

  std::string topic = (std::string) "/MERGEtoMQTT/Sensor/" +sensorname.c_str();
  Log.notice(F(" publiSensor %s : %s " CR), topic.c_str(), payload.c_str());
  pub(topic.c_str(), payload.c_str(), 1);
}

void unSubId(std::string id) {

  char topicH[mqtt_topic_max_size];
  strcpy(topicH, mqtt_topic);
  strcat(topicH, gateway_name);
  strcat(topicH, id.c_str() );
  strcat(topicH, (char*)"/#");

  if (client.unsubscribe(topicH)) {
    Log.notice(F(" unsubscrib : %s" CR), topicH);
  }
}

int currentid = 0;
void LoopMergeTemp() {
  std::string currentTopic;
  switch (stateid) {
    case 0:
      if (stateid < idDeviceList.Count()) {
        fechtSubId(idDeviceList[0]);
        currentid = 0;
        stateid++;
      }
      break;
    default:
      if (swichtid_signal == 2) {
        swichtid_signal = 0;
        pos = 0;
        minpos = 0xffffffff;
        unSubId(idDeviceList[currentid]);
        if (stateid < idDeviceList.Count()) {
          fechtSubId(idDeviceList[stateid]);
          currentid = stateid;
          stateid++;

        } else
          stateid = 0;
      }
  }

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
  }

  int day2000 = 10957; //days_from_civil(2000, 1, 1);

  int newsecondnow = (days_from_civil(timeinfo.tm_year + 1900, (timeinfo.tm_mon) + 1, timeinfo.tm_mday) - day2000) * 24 * 3600 +
                     (timeinfo.tm_hour) * 3600 +
                     timeinfo.tm_min * 60 +
                     timeinfo.tm_sec;

  /*  int newsecondnow = (timeinfo.tm_year + 1900 - 2000) * 31536000 +
                     (timeinfo.tm_mon) * 2629743 + // environ
                     timeinfo.tm_mday * 86400 +
                     (timeinfo.tm_hour) * 3600 +
                     timeinfo.tm_min * 60 +
                     timeinfo.tm_sec;
*/

  //Log.notice(F("newsecondnow: %d  secondnow %d " CR), newsecondnow,secondnow);

  char horoout[200];

  char horstamp[200];
  char n1[200];
  char t1[200];
  DateArray[0] = 0;
  Tearray[0] = 0;
  vbatarray[0] = 0;
  minarray[0] = 0;
  maxarray[0] = 0;
  //chargeArray[0]=0;

  // strcat(DateArray, "[");
  // strcat(Tearray, "[");

  if (secondnow != 0xffffffff && pos > 0 && newsecondnow > secondnow + 5) {
    Serial.println("\r\n  STRCALCUL");
    secondnow = 0xffffffff;

    Log.notice(F("count: %d" CR), pos);

    int nbr = reduit(pos);
    Log.notice(F(" Value: %u" CR), nbr);

    Histo.clear();
    int y = 0;
    for (y = 0; y < nbr; y++) {
      if (y != 0) {
        strcat(DateArray, ",");
        strcat(Tearray, ",");
        strcat(vbatarray, ",");
        strcat(minarray, ",");
        strcat(maxarray, ",");
        //   strcat(chargeArray, ",");
      }

      /*    int second = (stoi(tokens[2]) - 2000) * 31536000 +
                 stoi(tokens[1]) * 2629743 + // environ
                 stoi(tokens[0]) * 86400 +
                 stoi(tokens[3]) * 3600 +
                 stoi(tokens[4]) * 60 +
                 stoi(tokens[5]);
*/
      uint reste = shorttime[y];

      int day2000 = days_from_civil(2000, 1, 1);

      int resJ = reste / (24 * 3600) + day2000; //day
      int resteSJ = reste - (resJ - day2000) * (24 * 3600);

      std::tuple<unsigned, unsigned, unsigned> rt;
      rt = civil_from_days(resJ);

      /*    int year = reste / (uint)31536000;
      reste -= (year * (uint)31536000);
      int month = reste / (uint)2629743;
      reste -= (month * (uint)2629743);
      int day = reste / (uint)86400;
      reste -= (day * (uint)86400);
*/

      int year = std::get<0>(rt);
      int month = std::get<1>(rt);
      int day = std::get<2>(rt);

      int heure = resteSJ / (uint)3600;
      resteSJ -= (heure * (uint)3600);
      int min = resteSJ / (uint)60;

      sprintf(horoout, "%.2d-%.2d-%.2d %.2d:%.2d", year, month, day, heure, min); // year depus 2000 ajout 1 mouth

      sprintf(horstamp, "%.2d-%.2d-%.2d", year, month, day); // year depus 2000

      //   sprintf(horoout, "%.2d-%.2d %.2d:%.2d", day, month, heure, min); // year depus 2000
      sprintf(n1, "j%d", y);
      sprintf(t1, "t%d", y);

      char ft[30];
      sprintf(ft, "%.2f", (float)shorttempv[y] / 100);

      char batt[30];
      sprintf(batt, "%i", shortvbat[y]);

      char mmin[30];
      sprintf(mmin, "%.2f", (float)mintemp[y] / 100);

      char mama[30];
      sprintf(mama, "%.2f", (float)maxtemp[y] / 100);

      //     char cb[30];
      //     sprintf(cb, "%i", shortcharge[y] );

      strcat(DateArray, horoout);
      strcat(Tearray, ft);
      strcat(vbatarray, batt);
      strcat(minarray, mmin);
      strcat(maxarray, mama);
      //
      //    strcat(chargeArray, cb);

      //  Histo[ n1 ]= horoout;
      // Histo[ t1 ]= (float)shorttempv[y] / 100 ;

      Log.notice(F("cnt: %d:  timeS:%u  hor: %s  temp:%F min:%F  max:%F " CR), y, (uint)shorttime[y], horoout, (float)shorttempv[y] / 100,
                 (float)mintemp[y] / 100, (float)maxtemp[y] / 100);
    }

    //   strcat(DateArray, "]");
    //   strcat(Tearray, "]");

    static char payload[6000];
    serializeJson(copyMERGETEMPdata, payload, 6000);

    std::string name = copyMERGETEMPdata["name"];
    std::string topic = (std::string) "/MERGEtoMQTT/" + name + (std::string) "/LastMessage";
    Log.notice(F(" publi name %s : %s " CR), topic.c_str(), payload);
    pub(topic.c_str(), payload, 1);

    Histo["model"] = "ESP32TEMP";
    Histo["name"] = name;

    Histo["Date"] = DateArray;
    Histo["Temp"] = Tearray;
    Histo["Vbatt"] = vbatarray;
    Histo["tmin"] = minarray;
    Histo["tmax"] = maxarray;
    // Histo["Charge"] =chargeArray;

    Histo["nbItem"] = y;
    Histo["Jour_tmin"] = (float)Jour_tmmintemp / 100;
    Histo["Jour_tmax"] = (float)Jour_tmmaxtemp / 100,

    sprintf(horoout, "%.2d-%.2d-%.2d %.2d:%.2d:%.2d",
            (timeinfo.tm_year) + 1900,
            timeinfo.tm_mon + 1,
            timeinfo.tm_mday,
            timeinfo.tm_hour,
            timeinfo.tm_min,
            timeinfo.tm_sec);
    Histo["Stamp"] = horoout;

    serializeJson(Histo, payload, 4000);
    std::string topic2 = (std::string) "/MERGEtoMQTT/" + name + (std::string) "/Histo/" + (std::string)horstamp;
    Serial.println((char*)"*Publ\r\n");
    Serial.println((char*)topic2.c_str());
    Serial.println((char*)"\r\n");
    Serial.println((char*)payload);
    // Log.notice(F(" publi name %s : %s " CR), topic2.c_str(), payload2);
    swichtid_signal = 1;
    if (y != 0) {
      pub(topic2.c_str(), (char*)payload, 1);
      Serial.println(" \r\npublised");
    } else
      Serial.println(" \r\nnot publised");
  }
}

//---------aide ---------------------------------------------------------------------------

std::vector<std::string> split(const std::string& text, char sep) {
  std::vector<std::string> tokens;
  std::size_t start = 0, end = 0;
  while ((end = text.find(sep, start)) != std::string::npos) {
    tokens.push_back(text.substr(start, end - start));
    start = end + 1;
  }
  tokens.push_back(text.substr(start));
  return tokens;
}
void setdot(int x, int16_t temp, int timet, int vbatt, int16_t maxtempin, int16_t mintempin) {
  shorttempv[x] = temp;
  shorttime[x] = timet;
  shortvbat[x] = vbatt;
  maxtemp[x] = maxtempin;
  mintemp[x] = mintempin;
  //  shortcharge[x] = charge;
}

//---------------reduit---------------------------------------------------------------------

int reduit(int posmax) {
  int datasize = posmax;
  int shortdatasize = NBITESHORT;
  int step = (int)((float)datasize / (float)shortdatasize + 0.5);
  int x, y;
  int temp, vbatt; //, charge;
  int mint, maxt;
  int16_t tmmaxtemp, tmmintemp;
  if (step == 0) {
    Log.notice(F("force step=1"));
    step = 1; //return 0;
  }
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
  }

  int datejoursec = (timeinfo.tm_year + 1900 - 2000) * 31536000 +
                    (timeinfo.tm_mon) * 2629743 + // environ
                    timeinfo.tm_mday * 86400;

  Jour_tmmintemp = 0x7fff;
  Jour_tmmaxtemp = 0;

  for (x = 0; x < datasize; x += step) {
    temp = 0;
    vbatt = 0;
    //charge = 0;

    tmmintemp = 0x7fff;
    tmmaxtemp = 0x8000;

    mint = bigtime[x];
    maxt = mint;

    for (y = 0; y < step && (x + y) < datasize; y++) {
      temp += bigtempv[x + y];
      vbatt += bigvbat[x + y];

      if (bigtempv[x + y] > tmmaxtemp)
        tmmaxtemp = bigtempv[x + y];
      if (bigtempv[x + y] < tmmintemp)
        tmmintemp = bigtempv[x + y];

      //    if (bigtime[x + y] >= datejoursec)
      //  {
      if (bigtempv[x + y] > Jour_tmmaxtemp)
        Jour_tmmaxtemp = bigtempv[x + y];
      if (bigtempv[x + y] < Jour_tmmintemp)
        Jour_tmmintemp = bigtempv[x + y];

      //        Log.notice(F(" keep   " CR));

      //   }

      //    charge += bigcharge[x + y];
      maxt = bigtime[x + y];
//      Log.notice(F(" bigtime %d  bigtempv %d   bigvbat %d   datejoursec %d " CR), bigtime[x + y], bigtempv[x + y], bigvbat[x + y], datejoursec); //, bigcharge[x + y]);
    }

    if (y == 0)
      y = 1;
    temp /= y; // sera step sauf a la fin
    vbatt /= y;
    // charge /= y;
    Log.notice(F(" wr:%d  x:%d  y:%d   step %d posmax %d  mint %d  maxt %d   ewart %d   tmmintemp %d tmmaxtemp %d" CR), (x / step), x, y, step, posmax, mint, maxt, mint + (maxt - mint) / 2, tmmintemp, tmmaxtemp);
    setdot((x / step), temp, maxt /* + (maxt - mint) / 2*/, vbatt, tmmintemp, tmmaxtemp); //, charge);
    if (x / step >= NBITESHORT)
      break;
  }
  return (x / step);
}

//----------------MQTTtoMERGETEMP-------------------------------------------
// ici on recoit tout les messages "/LORAtoMQTT/Yaourt1/Historisque/#"  "TempCelsius"
// ici on recoit tout les messages "/LORAtoMQTT/Yaourt1/Histo/XXXX-xx-xx"  date du cumul jour

// le cumul du jour est realiser a chaque message ;  les cumul des jours passer doivent etre reposter regulierement pour ne pas etre effacer par le serveur
// on gere une machine d'etat pour realiser les cumul par noeud "yaourtx";
//  la carte doit rebooter une fois par jour au mini pour reposter les anciens messages qui sinon vont s'effacer

void MQTTtoMERGETEMP(char* topicOri, JsonObject& MERGETEMPdata) { // json object decoding
  bool success = false;
  static char payloadjson[4000];
  int datesecond;

  Log.notice(F(" get message  on  %s " CR), topicOri);

  if (strstr(topicOri, "1-01-1970") != 0) {
    Log.notice(F(" ignore datee on   %s " CR), topicOri);
    return;
  }

  //note la date de reception du dernier message ; memorise pour time out
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
  }
  //    Log.notice(F(" tm_year  %u " CR),timeinfo.tm_year );

  /* secondnow = (timeinfo.tm_year + 1900 - 2000) * 31536000 +
              (timeinfo.tm_mon) * 2629743 + // environ
              timeinfo.tm_mday * 86400 +
              (timeinfo.tm_hour) * 3600 +
              timeinfo.tm_min * 60 +
             timeinfo.tm_sec;
*/

  // reposte ancien cumul journalié

  if (MERGETEMPdata.containsKey("tmin") && MERGETEMPdata.containsKey("Date") && MERGETEMPdata.containsKey("Stamp")) { //"/LORAtoMQTT/Yaourt1/Histo/XXXX-xx-xx"
    // reposte les jours anterieurs

    char horstamp[200];
    sprintf(horstamp, "%.2d-%.2d-%.2d",
            (timeinfo.tm_year) + 1900,
            timeinfo.tm_mon + 1,
            timeinfo.tm_mday);

    std::string dateorg = MERGETEMPdata["Date"];
    int posmr = dateorg.find(horstamp);
    Log.notice(F(" date.find: %u " CR), posmr);

    if (horstamp != MERGETEMPdata["Stamp"] && posmr != 0) {
      MERGETEMPdata["Stamp"] = horstamp;

      std::string topic = topicOri;
      serializeJson(MERGETEMPdata, payloadjson, 4000);

      int posmr = topic.find("/MERGEtoMQTT/");
      topic = topic.substr(posmr, 1000);
      Log.notice(F(" reposte: old cumuljour  %s : %s" CR), topic.c_str());
      pub(topic.c_str(), payloadjson, 1);

      return;
    }
  }

  // traite nouvelle mesure

  std::string topic = topicOri;
  int poshis = topic.find("History");  //pour les lora

 int posone = topic.find("OneWiretoMQTT");  //pour les local
  

  if (MERGETEMPdata.containsKey("TempCelsius") && ( poshis > 0 || posone>0)  ) { ///LORAtoMQTT/Yaourt1/historisque/#
      std::string temp = MERGETEMPdata["TempCelsius"];
    if (temp=="85")  // ignore car bug reset sur module lora
       return;

    int day2000 = 10957; //days_from_civil(2000, 1, 1);

    datesecond = (days_from_civil(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday) - day2000) * 24 * 3600;
    secondnow = datesecond +
                (timeinfo.tm_hour) * 3600 +
                timeinfo.tm_min * 60 +
                timeinfo.tm_sec;

    //  serializeJson(MERGETEMPdata, payloadjson, 4000);
    //  Log.notice(payloadjson);

    

    std::string Vbatt = "0";
    if (MERGETEMPdata.containsKey("Vbatt")) {
      std::string Vbatt1 = MERGETEMPdata["Vbatt"];
      Vbatt = Vbatt1;
    }
    //  std::string Charge = MERGETEMPdata["Charge"];

    //Time":"13-11-2023/18-55-28"
    String Ttime = MERGETEMPdata["Time"];

    Ttime.replace("/", "-");

    std::string time = Ttime.c_str();
    std::vector<std::string> tokens;
    tokens = split(time, '-');

    /*  int second = (stoi(tokens[2]) - 2000) * 31536000 +
                 (stoi(tokens[1]) - 1) * 2629743 + // environ       doit commencer a 0
                 stoi(tokens[0]) * 86400 +
                 stoi(tokens[3]) * 3600 +
                 stoi(tokens[4]) * 60 +
                 stoi(tokens[5]);
*/
    //   int day2000 = 10957; //days_from_civil(2000, 1, 1);

    int jourdateinsecond = (days_from_civil(stoi(tokens[2]), stoi(tokens[1]), stoi(tokens[0])) - day2000) * 24 * 3600;

    int secondinmessage = jourdateinsecond + stoi(tokens[3]) * 3600 + stoi(tokens[4]) * 60 + stoi(tokens[5]);

    /*  int jourdateinsecond = (stoi(tokens[2]) - 2000) * 31536000 +
                           (stoi(tokens[1]) - 1) * 2629743 + // environ       doit commencer a 0
                           stoi(tokens[0]) * 86400;
*/

    // Log.notice(F(":INPUT day2000:%d   secondin %d" CR), day2000, second);

    // Log.notice(F(":INPUT Ttime:%s temp:%s  second:%u  datesecond:%u  inf?%u" CR),  Ttime.c_str(), temp.c_str(), jourdateinsecond , datesecond , jourdateinsecond < datesecond);

    if (jourdateinsecond < datesecond) //filtre
    {
      //      Log.notice(F(" ignore  %s " CR), Ttime.c_str());
      return; //ignore
    }

    serializeJson(MERGETEMPdata, payloadjson, 4000);

    if (secondinmessage < minpos) {
   
      pos = 0;
      Log.notice(F(" remise a O  " CR));
    }

   minpos = secondinmessage;

    bigtime[pos] = secondinmessage;
    bigtempv[pos] = stof(temp) * 100;
    bigvbat[pos] = stoi(Vbatt);
    // bigcharge[pos] = stoi(Charge);

    //sauvegarde recu pour voir si dernier apres time out
    //Log.notice(F(" fin:   temp:%s  Vbatt:%s" CR),temp.c_str() ,Vbatt.c_str());
    copyMERGETEMPdata.clear();

    //    Log.notice(F("deserializeJson" CR));
    deserializeJson(copyMERGETEMPdata, payloadjson, 4000);

    if (pos < NBITE - 1)
      pos++;

    Log.notice(F(" pos  %u " CR), pos);
    //  if (pos | 1) {
    //    Log.notice(F(" erase topic  %s " CR),topicOri+20);

    //  pub(topicOri+20, "{}", 1);

    // home/OMG_ESP32_LORA2
    //     erasebMQTTtopic(topicOri+20, 1);
    //  }

  } 
}

#endif
