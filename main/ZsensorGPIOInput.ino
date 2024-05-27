#include "User_config.h"
#include <time.h> // Inclure la bibliothèque pour l'heure

#ifdef ZsensorGPIOInput
#  if defined(TRIGGER_GPIO) && INPUT_GPIO == TRIGGER_GPIO
unsigned long resetTime = 0;
#  endif
unsigned long lastDebounceTime = 0;
int InputState = 3; // Set to 3 so that it reads on startup
int previousInputState = 3;
unsigned long risingEdgeCount = 0; // Compteur de fronts montants

void setupGPIOInput() {
  Log.notice(F("Reading GPIO at pin: %d" CR), INPUT_GPIO);
  pinMode(INPUT_GPIO, GPIO_INPUT_TYPE); // declare GPIOInput pin as input_pullup to prevent floating. Pin will be high when not connected to ground
}

void MeasureGPIOInput() {
  int reading = digitalRead(INPUT_GPIO);

  // check to see if you just pressed the button
  // (i.e. the input went from LOW to HIGH), and you've waited long enough
  // since the last press to ignore any noise:

  // If the switch changed, due to noise or pressing:
  if (reading != previousInputState) {
    // reset the debouncing timer
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > GPIOInputDebounceDelay) {
    // whatever the reading is at, it's been there for longer than the debounce
    // delay, so take it as the actual current state:
#  if defined(ESP8266) || defined(ESP32)
    yield();
#  endif
#  if defined(TRIGGER_GPIO) && INPUT_GPIO == TRIGGER_GPIO && !defined(ESPWifiManualSetup)
    if (reading == LOW) {
      if (resetTime == 0) {
        resetTime = millis();
      } else if ((millis() - resetTime) > 3000) {
        Log.trace(F("Button Held" CR));
        InfoIndicatorOFF();
        SendReceiveIndicatorOFF();
// Switching off the relay during reset or failsafe operations
#    ifdef ZactuatorONOFF
        uint8_t level = digitalRead(ACTUATOR_ONOFF_GPIO);
        if (level == ACTUATOR_ON) {
          ActuatorTrigger();
        }
#    endif
        Log.notice(F("Erasing ESP Config, restarting" CR));
        setup_wifimanager(true);
      }
    } else {
      resetTime = 0;
    }
#  endif
    // if the Input state has changed:
    if (reading != InputState) {
      Log.trace(F("Creating GPIOInput buffer" CR));
      StaticJsonDocument<JSON_MSG_BUFFER> GPIOdataBuffer;
      JsonObject GPIOdata = GPIOdataBuffer.to<JsonObject>();
      
      // Obtenir l'heure locale
      struct tm timeinfo;
      if (!getLocalTime(&timeinfo)) {
        Log.error(F("Failed to obtain time" CR));
      } else {
        char timeString[20];
        strftime(timeString, sizeof(timeString), "%Y-%m-%d %H:%M:%S", &timeinfo);
        GPIOdata["timestamp"] = timeString; // Ajouter l'horodatage
      }

      if (reading == HIGH) {
        GPIOdata["gpio"] = "HIGH";
      }
      if (reading == LOW) {
        GPIOdata["gpio"] = "LOW";
      }

      // Incrémentation du compteur de fronts montants
      if (reading == HIGH ) {
        risingEdgeCount++;
      }

      // Ajouter le compteur aux données JSON
      GPIOdata["risingEdgeCount"] = risingEdgeCount;

      GPIOdata["origin"] = subjectGPIOInputtoMQTT;
      handleJsonEnqueue(GPIOdata);

#  if defined(ZactuatorONOFF) && defined(ACTUATOR_TRIGGER)
      //Trigger the actuator if we are not at startup
      if (InputState != 3) {
#    if defined(ACTUATOR_BUTTON_TRIGGER_LEVEL)
        if (reading == ACTUATOR_BUTTON_TRIGGER_LEVEL)
          ActuatorTrigger(); // Button press trigger
#    else
        ActuatorTrigger(); // Switch trigger
#    endif
      }
#  endif
      InputState = reading;
    }
  }

  // save the reading. Next time through the loop, it'll be the previousInputState:
  previousInputState = reading;
}
#endif
