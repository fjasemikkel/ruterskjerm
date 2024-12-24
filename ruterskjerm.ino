#include <WiFi.h>
#include <HTTPClient.h>
#include <TFT_eSPI.h>
#include <ArduinoJson.h>
#include <time.h>

// WiFi-konfigurasjon *** Bytt ut med egne detaljer
const char* ssid = «SSID»;
const char* password = «password»;

// API-konfigurasjon
const char* apiUrl = "https://api.entur.io/journey-planner/v3/graphql";

// GraphQL-spørring (formatert som én streng) BYTT UT 59766 med ditt stoppested (stoppested.entur.org)
const char* query = "{\"query\":\"{ stopPlace(id: \\\"NSR:StopPlace: 59766\\\») { id name estimatedCalls(timeRange: 72100, numberOfDepartures: 6) { expectedArrivalTime destinationDisplay { frontText } serviceJourney { journeyPattern { line { id } } } } } }\"}";

// TFT-skjermoppsett
TFT_eSPI tft = TFT_eSPI();

// Tidsserver og tidssone
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 3600; // Tidssone (GMT+1)
const int daylightOffset_sec = 3600; // Sommertid

// Forrige klokkeslett for å unngå flicker
char lastTimeString[9] = "";
unsigned long lastUpdateTime = 0; // Tidspunkt for siste oppdatering av avganger

// Funksjon for å hente nåværende tid som struct tm
tm getCurrentTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Klarte ikke å hente tid");
  }
  return timeinfo;
}

// Funksjon for å beregne minutter til neste avgang
int calculateMinutesToNextDeparture(String expectedArrival) {
  tm currentTime = getCurrentTime();

  // Hent nåværende tid
  int currentHour = currentTime.tm_hour;
  int currentMinute = currentTime.tm_min;

  // Hent forventet ankomsttid
  int arrivalHour = expectedArrival.substring(11, 13).toInt();
  int arrivalMinute = expectedArrival.substring(14, 16).toInt();

  // Beregn forskjellen i minutter
  int diffMinutes = (arrivalHour * 60 + arrivalMinute) - (currentHour * 60 + currentMinute);
  return diffMinutes < 0 ? diffMinutes + 1440 : diffMinutes; // Håndter over midnatt
}

void drawClock() {
  tm currentTime = getCurrentTime();
  char timeString[9]; // HH:MM:SS
  sprintf(timeString, "%02d:%02d:%02d", currentTime.tm_hour, currentTime.tm_min, currentTime.tm_sec);

  if (strcmp(lastTimeString, timeString) != 0) {
    tft.setTextFont(4); // Bruk en større og jevnere innebygd font
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.fillRect(200, 0, 80, 20, TFT_BLACK); // Tøm området for klokken
    tft.setCursor(200, 0);
    tft.print(timeString);
    strcpy(lastTimeString, timeString);
  }
}

void drawDepartures(JsonArray estimatedCalls, const char* stopName) {
  tft.fillRect(0, 20, 240, 220, TFT_BLACK); // Oppdater kun dette området
  tft.setCursor(0, 20); // Start under klokken
  tft.setTextFont(2); // Bruk en mindre innebygd font for avgangsdata
  tft.printf("Avganger fra %s:\n", stopName);

  int yOffset = 50; // Startpunkt for avgangsdata

  for (JsonObject call : estimatedCalls) {
    const char* rawLineId = call["serviceJourney"]["journeyPattern"]["line"]["id"];
    const char* destination = call["destinationDisplay"]["frontText"];
    const char* expectedArrival = call["expectedArrivalTime"];

    // Fjern "RUT:Line:" fra linjenummeret
    String lineId = String(rawLineId);
    lineId.replace("RUT:Line:", "");

    // Beregn minutter til neste avgang
    int minutesToDeparture = calculateMinutesToNextDeparture(String(expectedArrival));

    // Skriv ut avgangsinformasjon
    tft.setCursor(0, yOffset);
    tft.printf("%-4s %-12s %4d min", lineId.c_str(), destination, minutesToDeparture);
    yOffset += 30; // Flytt til neste linje
  }
}

void connectWiFi() {
  tft.fillRect(0, 0, 240, 20, TFT_BLACK); // Fjern WiFi-statusområdet
  tft.setCursor(0, 0);
  tft.setTextFont(2);
  tft.println("Kobler til WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    tft.print(".");
  }
  tft.fillRect(0, 0, 240, 20, TFT_BLACK); // Fjern eventuelle prikker
  tft.setCursor(0, 0);
  tft.println("WiFi tilkoblet!");
}

void setup() {
  // Initialiser seriell kommunikasjon og TFT-skjerm
  Serial.begin(115200);
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  // Koble til WiFi
  connectWiFi();

  // Konfigurer NTP
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
}

void loop() {
  drawClock();

  unsigned long currentTime = millis();
  if (WiFi.status() == WL_CONNECTED && (currentTime - lastUpdateTime > 30000)) {
    lastUpdateTime = currentTime; // Oppdater tid for siste avgangsforespørsel

    HTTPClient http;

    // Begynn forespørselen
    http.begin(apiUrl);
    http.addHeader("Content-Type", "application/json");

    // Send forespørselen
    int httpCode = http.POST(query);

    // Håndter responsen
    if (httpCode == 200) {
      String response = http.getString();
      Serial.println("Respons mottatt:");
      Serial.println(response);

      // Parse JSON-responsen
      DynamicJsonDocument doc(8192);
      DeserializationError error = deserializeJson(doc, response);

      if (error) {
        Serial.print("JSON parsing error: ");
        Serial.println(error.c_str());
        return;
      }

      // Hent stoppested og avgangsdata
      JsonObject stopPlace = doc["data"]["stopPlace"];
      const char* stopName = stopPlace["name"];
      JsonArray estimatedCalls = stopPlace["estimatedCalls"];

      drawDepartures(estimatedCalls, stopName);
    } else {
      // Feilhåndtering for HTTP-forespørsel
      tft.fillScreen(TFT_BLACK);
      tft.setCursor(0, 0);
      tft.printf("HTTP-feil: %d\n", httpCode);
      Serial.printf("HTTP-feil: %d\n", httpCode);
      Serial.println(http.getString());
    }

    http.end();
  } else if (WiFi.status() != WL_CONNECTED) {
    // Hvis WiFi er frakoblet
    tft.fillScreen(TFT_BLACK);
    tft.setCursor(0, 0);
    tft.println("WiFi frakoblet! Kobler til...");
    WiFi.reconnect();
  }

  delay(1000); // Oppdater klokken hvert sekund
}
