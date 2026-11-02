#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <FS.h>
#include <LittleFS.h>
#include <time.h>
#include <math.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WiFiUdp.h>

// =====================================================
// ESP32 POCKETOS
// ESP32 DevKit V1 + SSD1306 128x64 I2C OLED
//
// Buttons:
// UP     -> GPIO 32
// DOWN   -> GPIO 33
// SELECT -> GPIO 25
// BACK   -> GPIO 26
// HOME   -> GPIO 27
//
// OLED:
// SDA -> GPIO 21
// SCL -> GPIO 22
//
// Apps:
// Home, Notes, Files, Calculator, Clock, Settings, Web AdBlock
// =====================================================

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_ADDRESS 0x3C

Adafruit_SSD1306 display(
  SCREEN_WIDTH,
  SCREEN_HEIGHT,
  &Wire,
  OLED_RESET
);

// ---------------- BUTTON PINS ----------------

const int PIN_UP = 32;
const int PIN_DOWN = 33;
const int PIN_SELECT = 25;
const int PIN_BACK = 26;
const int PIN_HOME = 27;

// ---------------- APP STATES ----------------

enum Screen {
  HOME,
  NOTES,
  FILES,
  CALCULATOR,
  CLOCK_APP,
  SETTINGS,
  WEB_ADBLOCK
};

Screen currentScreen = HOME;
Screen previousScreen = HOME;

// ---------------- BUTTON EVENTS ----------------

enum Button {
  BTN_NONE,
  BTN_UP,
  BTN_DOWN,
  BTN_SELECT,
  BTN_BACK,
  BTN_HOME,
  BTN_LEFT,
  BTN_RIGHT
};

struct ButtonState {
  int pin;
  bool stableState;
  bool lastReading;
  unsigned long lastChange;
  unsigned long pressStart;
};

ButtonState buttons[] = {
  {PIN_UP,     HIGH, HIGH, 0, 0},
  {PIN_DOWN,   HIGH, HIGH, 0, 0},
  {PIN_SELECT, HIGH, HIGH, 0, 0},
  {PIN_BACK,   HIGH, HIGH, 0, 0},
  {PIN_HOME,   HIGH, HIGH, 0, 0}
};

const unsigned long DEBOUNCE_MS = 30;
const unsigned long LONG_PRESS_MS = 450;

// ---------------- HOME MENU ----------------

const char* appNames[] = {
  "Notes",
  "Files",
  "Calculator",
  "Clock",
  "Settings",
  "Web AdBlock"
};

const int APP_COUNT = 6;
int homeSelection = 0;

// ---------------- STORAGE ----------------

// Maximum length of a note in RAM.
const int NOTE_CAPACITY = 1200;

char noteBuffer[NOTE_CAPACITY + 1];
int noteLength = 0;
bool noteDirty = false;

String fileNames[20];
int fileCount = 0;
int fileSelection = 0;

// The currently opened file.
String currentFile = "/note1.txt";
int nextNoteNumber = 1;

// ---------------- NOTEPAD ----------------

// Characters are selected using UP/DOWN and inserted
// using SELECT.
//
// Keyboard pages:
// 0 = uppercase
// 1 = lowercase
// 2 = numbers and punctuation

const char* keyboardPages[] = {
  "ABCDEFGHIJKLMNO",
  "abcdefghijklmno",
  "0123456789.,!?-"
};

const int KEY_COUNT = 15;
int keyboardPage = 0;
int keyboardSelection = 0;
bool noteMenuOpen = false;
int noteMenuSelection = 0;

// ---------------- CALCULATOR ----------------

// Simple two-operand calculator.
// Expression format: number operator number

String calcInput = "";
String calcResult = "";

const char* calcKeys[] = {
  "7", "8", "9", "/",
  "4", "5", "6", "*",
  "1", "2", "3", "-",
  "C", "0", "=", "+"
};

const int CALC_KEY_COUNT = 16;
int calcSelection = 0;

// ---------------- SETTINGS ----------------

bool invertDisplay = false;
int contrastValue = 200;
int settingsSelection = 0;

// ---------------- CLOCK ----------------

// Uptime clock works without Wi-Fi.
// It is reset when the ESP32 loses power.

unsigned long bootMillis = 0;

// ---------------- WEB ADBLOCK / DNS FILTER ----------------
// Connect ESP32 to the SAME Wi-Fi network as devices you want to filter.
// Set that device's DNS server manually to the ESP32's Wi-Fi IP address.
// NOTE: a phone cannot normally use its own hotspot as an upstream network
// and simultaneously join the ESP32 as a client. Use a router or a second
// hotspot/upstream device if the phone itself must be filtered by this ESP32.
const char* WIFI_SSID = "vivo 1907";
const char* WIFI_PASSWORD = "HAIVIVO1";

WebServer webServer(80);
bool webServerStarted = false;
WiFiUDP dnsUdp;       // listens for client DNS requests on UDP/53
WiFiUDP upstreamUdp;  // sends ordinary DNS requests to public resolver
bool dnsServerStarted = false;
int webSelection = 0;
unsigned long websitesSent = 0;
unsigned long dnsQueries = 0;
unsigned long dnsBlocked = 0;
unsigned long dnsForwarded = 0;
unsigned long dnsErrors = 0;
String pendingWebsiteName = "None yet";
String pendingWebsiteURL = "";

// Small starter blocklist. You can add domains here; suffix matching also
// blocks subdomains, e.g. pagead.example.com when example.com is listed.
const char* blockedDomains[] = {
  "doubleclick.net", "googlesyndication.com", "googleadservices.com",
  "adservice.google.com", "pagead2.googlesyndication.com",
  "adnxs.com", "adsrvr.org", "taboola.com", "outbrain.com",
  "criteo.com", "casalemedia.com", "adform.net", "advertising.com",
  "adroll.com", "serving-sys.com", "zedo.com", "popads.net",
  "propellerads.com", "adcolony.com", "applovin.com", "unityads.unity3d.com",
  "scorecardresearch.com", "analytics.yahoo.com"
};
const size_t BLOCKED_DOMAIN_COUNT = sizeof(blockedDomains) / sizeof(blockedDomains[0]);

const int WEB_SITE_COUNT = 4;
const char* webSiteNames[WEB_SITE_COUNT] = {
  "Wikipedia", "DuckDuckGo", "GitHub", "Example.com"
};
const char* webSiteURLs[WEB_SITE_COUNT] = {
  "https://www.wikipedia.org/",
  "https://duckduckgo.com/",
  "https://github.com/",
  "https://example.com/"
};

// ---------------- DRAW HELPERS ----------------

void drawHeader(const char* title) {
  display.clearDisplay();

  display.setTextSize(1);
  display.setTextColor(
    invertDisplay ? SSD1306_BLACK : SSD1306_WHITE
  );

  display.setCursor(0, 0);
  display.print(title);

  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);
}

void drawFooter(const char* text) {
  display.drawLine(0, 54, 127, 54, SSD1306_WHITE);
  display.setCursor(0, 57);
  display.print(text);
}

void drawSelection(
  int x,
  int y,
  int width,
  int height
) {
  display.drawRect(
    x, y, width, height, SSD1306_WHITE
  );
}

void finishDisplay() {
  display.display();
}

void showMessage(
  const char* title,
  const char* message,
  unsigned long duration = 1000
) {
  drawHeader(title);
  display.setCursor(0, 24);
  display.println(message);
  finishDisplay();
  delay(duration);
}

// ---------------- BUTTON HANDLING ----------------

Button readButton() {
  unsigned long now = millis();

  for (int i = 0; i < 5; i++) {
    bool reading = digitalRead(buttons[i].pin);

    if (reading != buttons[i].lastReading) {
      buttons[i].lastChange = now;
      buttons[i].lastReading = reading;
    }

    if (now - buttons[i].lastChange >= DEBOUNCE_MS &&
        reading != buttons[i].stableState) {

      buttons[i].stableState = reading;

      if (reading == LOW) {
        // Button has been pressed.
        buttons[i].pressStart = now;
      } else {
        // Button has been released.
        unsigned long held =
          now - buttons[i].pressStart;

        // Long UP = LEFT; long DOWN = RIGHT.
        if (i == 0 && held >= LONG_PRESS_MS) {
          return BTN_LEFT;
        }

        if (i == 1 && held >= LONG_PRESS_MS) {
          return BTN_RIGHT;
        }

        // Short press events.
        switch (i) {
          case 0: return BTN_UP;
          case 1: return BTN_DOWN;
          case 2: return BTN_SELECT;
          case 3: return BTN_BACK;
          case 4: return BTN_HOME;
        }
      }
    }
  }

  return BTN_NONE;
}
// ---------------- FILE STORAGE ----------------

bool saveNote() {
  File file = LittleFS.open(currentFile, "w");

  if (!file) {
    return false;
  }

  size_t written = file.write(
    (const uint8_t*)noteBuffer,
    noteLength
  );

  file.close();

  if (written != (size_t)noteLength) {
    return false;
  }

  noteDirty = false;
  return true;
}

bool loadNote(const String& path) {
  File file = LittleFS.open(path, "r");

  if (!file) {
    return false;
  }

  noteLength = 0;

  while (
    file.available() &&
    noteLength < NOTE_CAPACITY
  ) {
    noteBuffer[noteLength++] = file.read();
  }

  noteBuffer[noteLength] = '\0';
  file.close();

  currentFile = path;
  noteDirty = false;

  return true;
}

void createNewNote() {
  noteLength = 0;
  noteBuffer[0] = '\0';
  noteDirty = false;

  // Find an unused filename.
  while (
    LittleFS.exists(
      ("/note" + String(nextNoteNumber) + ".txt").c_str()
    )
  ) {
    nextNoteNumber++;
  }

  currentFile =
    "/note" + String(nextNoteNumber) + ".txt";

  keyboardPage = 0;
  keyboardSelection = 0;
  noteMenuOpen = false;

  currentScreen = NOTES;
}

void refreshFiles() {
  fileCount = 0;

  File root = LittleFS.open("/");

  if (!root || !root.isDirectory()) {
    return;
  }

  File file = root.openNextFile();

  while (file && fileCount < 20) {
    if (!file.isDirectory()) {
      String name = file.name();

      // Display only text notes.
      if (name.endsWith(".txt")) {
        if (!name.startsWith("/")) {
          name = "/" + name;
        }

        fileNames[fileCount++] = name;
      }
    }

    file.close();
    file = root.openNextFile();
  }

  root.close();

  if (fileCount == 0) {
    fileSelection = 0;
  } else if (fileSelection >= fileCount) {
    fileSelection = fileCount - 1;
  }
}

// ---------------- HOME SCREEN ----------------

void drawHome() {
  drawHeader("PocketOS");

  display.setCursor(0, 12);
  display.print("ESP32 Mini Computer");

  for (int i = 0; i < APP_COUNT; i++) {
    int y = 22 + i * 6;

    if (i == homeSelection) {
      display.fillRect(
        0, y - 1, 128, 7, SSD1306_WHITE
      );

      display.setTextColor(SSD1306_BLACK);
      display.setCursor(3, y);
      display.print(">");
      display.print(appNames[i]);

      display.setTextColor(
        invertDisplay ? SSD1306_BLACK : SSD1306_WHITE
      );
    } else {
      display.setCursor(3, y);
      display.print(" ");
      display.print(appNames[i]);
    }
  }

  finishDisplay();
}

void openSelectedApp() {
  switch (homeSelection) {
    case 0:
      createNewNote();
      break;

    case 1:
      refreshFiles();
      currentScreen = FILES;
      break;

    case 2:
      calcInput = "";
      calcResult = "";
      calcSelection = 0;
      currentScreen = CALCULATOR;
      break;

    case 3:
      currentScreen = CLOCK_APP;
      break;

    case 4:
      currentScreen = SETTINGS;
      break;

    case 5:
      webSelection = 0;
      currentScreen = WEB_ADBLOCK;
      break;
  }
}

void handleHome(Button button) {
  if (button == BTN_UP) {
    homeSelection--;

    if (homeSelection < 0) {
      homeSelection = APP_COUNT - 1;
    }
  }

  if (button == BTN_DOWN) {
    homeSelection++;

    if (homeSelection >= APP_COUNT) {
      homeSelection = 0;
    }
  }

  if (button == BTN_SELECT) {
    openSelectedApp();
  }
}

// ---------------- NOTEPAD ----------------

void drawNotes() {
  drawHeader("NOTEPAD");

  display.setCursor(0, 13);
  display.print(currentFile);

  display.setCursor(0, 22);

  // Show the most recent lines that fit on the display.
  int start = 0;

  if (noteLength > 90) {
    start = noteLength - 90;
  }

  int shown = 0;

  for (int i = start; i < noteLength; i++) {
    char c = noteBuffer[i];

    if (c == '\n') {
      display.println();
      shown = 0;
    } else {
      display.write(c);
      shown++;

      if (shown >= 21) {
        display.println();
        shown = 0;
      }
    }
  }

  display.setCursor(0, 44);

  display.print("Key:");

  if (keyboardPage == 0) {
    display.print("ABC");
  } else if (keyboardPage == 1) {
    display.print("abc");
  } else {
    display.print("123");
  }

  display.print(" ");
  display.print(
    keyboardPages[keyboardPage][keyboardSelection]
  );

  if (noteDirty) {
    display.print(" *");
  }

  drawFooter("SEL:Add UP/DN BACK:Menu");
  finishDisplay();
}

void drawNoteMenu() {
  drawHeader("NOTE OPTIONS");

  const char* options[] = {
    "Save",
    "New note",
    "Change keyboard",
    "Back to editing"
  };

  for (int i = 0; i < 4; i++) {
    int y = 15 + i * 9;

    if (i == noteMenuSelection) {
      display.fillRect(
        0, y - 1, 128, 9, SSD1306_WHITE
      );

      display.setTextColor(SSD1306_BLACK);
      display.setCursor(3, y);
      display.print(">");
      display.print(options[i]);

      display.setTextColor(
        invertDisplay ? SSD1306_BLACK : SSD1306_WHITE
      );
    } else {
      display.setCursor(3, y);
      display.print(" ");
      display.print(options[i]);
    }
  }

  finishDisplay();
}

void handleNotes(Button button) {
  if (noteMenuOpen) {
    if (button == BTN_UP) {
      noteMenuSelection =
        (noteMenuSelection + 3) % 4;
    }

    if (button == BTN_DOWN) {
      noteMenuSelection =
        (noteMenuSelection + 1) % 4;
    }

    if (button == BTN_BACK) {
      noteMenuOpen = false;
    }

    if (button == BTN_SELECT) {
      switch (noteMenuSelection) {
        case 0:
          if (saveNote()) {
            showMessage("NOTEPAD", "Note saved!");
          } else {
            showMessage("ERROR", "Save failed!");
          }
          noteMenuOpen = false;
          break;

        case 1:
          if (noteDirty) {
            if (!saveNote()) {
              showMessage(
                "ERROR",
                "Save before new note!"
              );
              break;
            }
          }

          createNewNote();
          break;

        case 2:
          keyboardPage =
            (keyboardPage + 1) % 3;
          noteMenuOpen = false;
          break;

        case 3:
          noteMenuOpen = false;
          break;
      }
    }

    return;
  }

  if (button == BTN_UP) {
    keyboardSelection--;

    if (keyboardSelection < 0) {
      keyboardSelection = KEY_COUNT - 1;
    }
  }

  if (button == BTN_DOWN) {
    keyboardSelection++;

    if (keyboardSelection >= KEY_COUNT) {
      keyboardSelection = 0;
    }
  }

  if (button == BTN_SELECT) {
    if (noteLength < NOTE_CAPACITY) {
      noteBuffer[noteLength++] =
        keyboardPages[keyboardPage][keyboardSelection];

      noteBuffer[noteLength] = '\0';
      noteDirty = true;
    }
  }

  if (button == BTN_BACK) {
    noteMenuOpen = true;
    noteMenuSelection = 0;
  }
}

// ---------------- FILE MANAGER ----------------

void drawFiles() {
  drawHeader("FILE MANAGER");

  if (fileCount == 0) {
    display.setCursor(0, 24);
    display.println("No saved notes.");
    display.println("Open Notes to create one.");
  } else {
    int first = 0;

    if (fileSelection >= 5) {
      first = fileSelection - 4;
    }

    for (
      int i = first;
      i < fileCount && i < first + 5;
      i++
    ) {
      int y = 14 + (i - first) * 8;

      if (i == fileSelection) {
        display.fillRect(
          0, y - 1, 128, 8, SSD1306_WHITE
        );

        display.setTextColor(SSD1306_BLACK);
      }

      display.setCursor(2, y);
      display.print(i == fileSelection ? ">" : " ");
      display.print(fileNames[i]);

      display.setTextColor(
        invertDisplay ? SSD1306_BLACK : SSD1306_WHITE
      );
    }
  }

  drawFooter("SEL:Open BACK:Home");
  finishDisplay();
}

void handleFiles(Button button) {
  if (fileCount == 0) {
    if (button == BTN_SELECT) {
      currentScreen = HOME;
    }
    return;
  }

  if (button == BTN_UP) {
    fileSelection--;

    if (fileSelection < 0) {
      fileSelection = fileCount - 1;
    }
  }

  if (button == BTN_DOWN) {
    fileSelection++;

    if (fileSelection >= fileCount) {
      fileSelection = 0;
    }
  }

  if (button == BTN_SELECT) {
    if (loadNote(fileNames[fileSelection])) {
      currentScreen = NOTES;
      noteMenuOpen = false;
    } else {
      showMessage("ERROR", "Could not open note");
    }
  }
}

// ---------------- CALCULATOR ----------------

bool calculateExpression(
  const String& expression,
  double& answer
) {
  int opPosition = -1;
  char op = 0;

  // Find an operator, ignoring a leading minus sign.
  for (int i = 1; i < expression.length(); i++) {
    char c = expression[i];

    if (
      c == '+' ||
      c == '-' ||
      c == '*' ||
      c == '/'
    ) {
      opPosition = i;
      op = c;
      break;
    }
  }

  if (opPosition < 0) {
    return false;
  }

  String left = expression.substring(0, opPosition);
  String right = expression.substring(opPosition + 1);

  if (left.length() == 0 || right.length() == 0) {
    return false;
  }

  double a = left.toDouble();
  double b = right.toDouble();

  switch (op) {
    case '+':
      answer = a + b;
      break;

    case '-':
      answer = a - b;
      break;

    case '*':
      answer = a * b;
      break;

    case '/':
      if (b == 0) {
        return false;
      }

      answer = a / b;
      break;

    default:
      return false;
  }

  return isfinite(answer);
}

void drawCalculator() {
  drawHeader("CALCULATOR");

  // Expression line
  display.setCursor(0, 13);
  display.print("In:");

  String shownInput = calcInput;
  if (shownInput.length() > 19) {
    shownInput = shownInput.substring(
      shownInput.length() - 19
    );
  }
  display.print(shownInput);

  // Result line
  display.setCursor(0, 23);
  display.print("Ans:");

  String shownResult = calcResult;
  if (shownResult.length() > 15) {
    shownResult = shownResult.substring(0, 15);
  }
  display.print(shownResult);

  // Draw the 4 x 4 keypad.
  for (int i = 0; i < CALC_KEY_COUNT; i++) {
    int row = i / 4;
    int col = i % 4;

    int x = col * 32;
    int y = 32 + row * 8;

    if (i == calcSelection) {
      display.fillRect(x, y, 30, 8, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
      display.setCursor(x + 3, y);
      display.print(calcKeys[i]);

      display.setTextColor(
        invertDisplay ? SSD1306_BLACK : SSD1306_WHITE
      );
    } else {
      display.setCursor(x + 3, y);
      display.print(calcKeys[i]);
    }
  }

  finishDisplay();
}

void handleCalculator(Button button) {
  // Vertical navigation: one row at a time.
  if (button == BTN_UP) {
    calcSelection =
      (calcSelection + CALC_KEY_COUNT - 4)
      % CALC_KEY_COUNT;
  }

  if (button == BTN_DOWN) {
    calcSelection =
      (calcSelection + 4) % CALC_KEY_COUNT;
  }

  // Horizontal navigation: one key at a time.
  // Long-press UP = LEFT; long-press DOWN = RIGHT.
  if (button == BTN_LEFT) {
    calcSelection =
      (calcSelection + CALC_KEY_COUNT - 1)
      % CALC_KEY_COUNT;
  }

  if (button == BTN_RIGHT) {
    calcSelection =
      (calcSelection + 1) % CALC_KEY_COUNT;
  }

  // BACK deletes the last character, or exits if empty.
  if (button == BTN_BACK) {
    if (calcInput.length() > 0) {
      calcInput.remove(calcInput.length() - 1);
      calcResult = "";
    } else {
      currentScreen = HOME;
    }
  }

  if (button == BTN_SELECT) {
    const char* key = calcKeys[calcSelection];

    // Execute the expression.
    if (strcmp(key, "=") == 0) {
      double result;

      if (calculateExpression(calcInput, result)) {
        calcResult = String(result, 4);

        // Trim unnecessary trailing zeros.
        while (calcResult.indexOf('.') >= 0 &&
               calcResult.endsWith("0")) {
          calcResult.remove(calcResult.length() - 1);
        }
        if (calcResult.endsWith(".")) {
          calcResult.remove(calcResult.length() - 1);
        }
      } else {
        // Give a specific message for division by zero.
        if (calcInput.indexOf('/') >= 0 &&
            calcInput.substring(calcInput.indexOf('/') + 1).toFloat() == 0.0f &&
            calcInput.indexOf('/') < (int)calcInput.length() - 1) {
          calcResult = "Divide by zero";
        } else {
          calcResult = "Invalid expression";
        }
      }
    }
    // Clear the expression and result.
    else if (strcmp(key, "C") == 0) {
      calcInput = "";
      calcResult = "";
    }
    // Add a number or operator.
    else {
      // This calculator evaluates one operation at a time.
      // Start a fresh expression after a result.
      if (calcResult.length() > 0) {
        calcInput = "";
      }

      calcResult = "";

      if (calcInput.length() < 24) {
        calcInput += key;
      }
    }
  }
}

// ---------------- WEB ADBLOCK APP + DNS FILTER ----------------

String jsonEscape(const String& input) {
  String out;
  for (size_t i = 0; i < input.length(); i++) {
    char c = input[i];
    if (c == '"' || c == '\\') out += '\\';
    if (c == '\n' || c == '\r') continue;
    out += c;
  }
  return out;
}

bool isBlockedDomain(const String& domain) {
  String d = domain;
  d.toLowerCase();
  while (d.endsWith(".")) d.remove(d.length() - 1);
  for (size_t i = 0; i < BLOCKED_DOMAIN_COUNT; i++) {
    String blocked = blockedDomains[i];
    blocked.toLowerCase();
    if (d == blocked || (d.length() > blocked.length() &&
        d.endsWith("." + blocked))) return true;
  }
  return false;
}

// Decode the question's uncompressed QNAME. Returns the offset of its zero byte.
int decodeDnsQuestionName(const uint8_t* packet, int length, String& domain) {
  int pos = 12;
  domain = "";
  while (pos < length) {
    uint8_t labelLength = packet[pos];
    if (labelLength == 0) return pos;
    // A query QNAME should not contain compression pointers.
    if ((labelLength & 0xC0) != 0 || labelLength > 63 || pos + 1 + labelLength >= length)
      return -1;
    if (domain.length()) domain += ".";
    for (int i = 0; i < labelLength; i++) {
      char c = (char)packet[pos + 1 + i];
      domain += c;
    }
    pos += 1 + labelLength;
    if (domain.length() > 253) return -1;
  }
  return -1;
}

void sendDnsPacket(const IPAddress& clientIP, uint16_t clientPort,
                   const uint8_t* packet, size_t length) {
  dnsUdp.beginPacket(clientIP, clientPort);
  dnsUdp.write(packet, length);
  dnsUdp.endPacket();
}

void answerBlockedDns(const uint8_t* query, int queryLength, int qnameEnd,
                      uint16_t qtype, const IPAddress& clientIP, uint16_t clientPort) {
  uint8_t response[512];
  int questionLength = qnameEnd + 1 + 4 - 12;
  int responseLength = 12 + questionLength;
  if (responseLength > queryLength || responseLength > (int)sizeof(response)) return;
  memcpy(response, query, responseLength);
  // QR=1, RD copied, RA=1, RCODE=NOERROR.
  response[2] = 0x81;
  response[3] = 0x80;
  response[4] = 0; response[5] = 1; // QDCOUNT
  response[6] = 0; response[7] = (qtype == 1) ? 1 : 0; // A gets 0.0.0.0
  response[8] = 0; response[9] = 0;
  response[10] = 0; response[11] = 0;
  if (qtype == 1) {
    const uint8_t answer[] = {
      0xC0, 0x0C,             // name pointer to QNAME
      0x00, 0x01,             // TYPE A
      0x00, 0x01,             // CLASS IN
      0x00, 0x00, 0x00, 0x3C, // TTL 60 seconds
      0x00, 0x04,             // RDLENGTH
      0x00, 0x00, 0x00, 0x00  // 0.0.0.0
    };
    memcpy(response + responseLength, answer, sizeof(answer));
    responseLength += sizeof(answer);
  }
  sendDnsPacket(clientIP, clientPort, response, responseLength);
}

void answerDnsServFail(const uint8_t* query, int queryLength, int qnameEnd,
                       const IPAddress& clientIP, uint16_t clientPort) {
  uint8_t response[512];
  int responseLength = qnameEnd + 1 + 4;
  if (responseLength > queryLength || responseLength > (int)sizeof(response)) return;
  memcpy(response, query, responseLength);
  response[2] = 0x81;
  response[3] = 0x82; // SERVFAIL
  response[6] = 0; response[7] = 0;
  response[8] = 0; response[9] = 0;
  response[10] = 0; response[11] = 0;
  sendDnsPacket(clientIP, clientPort, response, responseLength);
}

void processDnsQueries() {
  if (!dnsServerStarted) return;
  int packetSize = dnsUdp.parsePacket();
  if (packetSize <= 0) return;

  IPAddress clientIP = dnsUdp.remoteIP();
  uint16_t clientPort = dnsUdp.remotePort();
  uint8_t query[512];
  int len = dnsUdp.read(query, min(packetSize, (int)sizeof(query)));
  if (len < 17) { dnsErrors++; return; }
  dnsQueries++;

  String domain;
  int qnameEnd = decodeDnsQuestionName(query, len, domain);
  if (qnameEnd < 0 || qnameEnd + 4 >= len) {
    dnsErrors++;
    return;
  }
  int qtypePos = qnameEnd + 1;
  uint16_t qtype = ((uint16_t)query[qtypePos] << 8) | query[qtypePos + 1];

  if (isBlockedDomain(domain)) {
    dnsBlocked++;
    answerBlockedDns(query, len, qnameEnd, qtype, clientIP, clientPort);
    return;
  }

  // Forward unblocked queries to Cloudflare DNS. This starter proxy handles
  // one query at a time to keep the implementation small for the ESP32.
  upstreamUdp.flush();
  if (!upstreamUdp.beginPacket(IPAddress(1, 1, 1, 1), 53) ||
      upstreamUdp.write(query, len) != (size_t)len || !upstreamUdp.endPacket()) {
    dnsErrors++;
    answerDnsServFail(query, len, qnameEnd, clientIP, clientPort);
    return;
  }

  unsigned long started = millis();
  while (millis() - started < 1300) {
    int replySize = upstreamUdp.parsePacket();
    if (replySize > 0) {
      uint8_t reply[512];
      int replyLen = upstreamUdp.read(reply, min(replySize, (int)sizeof(reply)));
      if (replyLen >= 12 && reply[0] == query[0] && reply[1] == query[1]) {
        dnsForwarded++;
        sendDnsPacket(clientIP, clientPort, reply, replyLen);
        return;
      }
    }
    delay(1);
  }
  dnsErrors++;
  answerDnsServFail(query, len, qnameEnd, clientIP, clientPort);
}

void handleWebRoot() {
  String html = R"HTML(<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1"><title>PocketOS DNS AdBlock</title><style>
  body{font-family:system-ui,Arial;margin:0;background:#101827;color:#f3f6fb;padding:18px}main{max-width:560px;margin:auto}section{background:#1b2739;border-radius:16px;padding:16px;margin:12px 0}h1{font-size:24px;margin:4px 0}p{line-height:1.45;color:#c9d4e5}.pill{display:inline-block;padding:5px 10px;border-radius:20px;background:#263e35;color:#a9f0c1}a.open{display:block;width:100%;box-sizing:border-box;border:0;border-radius:10px;padding:13px;margin-top:10px;background:#d8e7ff;color:#13213a;font-size:16px;font-weight:700;text-align:center;text-decoration:none}small{color:#aebbd0}.url{overflow-wrap:anywhere;font-size:13px;color:#aebbd0}
  </style></head><body><main><h1>🛡️ PocketOS DNS AdBlock</h1><p>The ESP32 filters DNS requests for devices that explicitly use its IP as their DNS server.</p><section><b>ESP32 connection</b><p id="wifi">Checking…</p><b>DNS filtering statistics</b><p id="stats">Waiting for DNS requests…</p><small>Blocked DNS requests are answered with 0.0.0.0 for A records (or no address for other record types). This is a small starter blocklist, not a complete Pi-hole replacement.</small></section><section><b>Latest website selected on PocketOS</b><p id="site">No website sent yet</p><div class="url" id="url"></div><a id="open" class="open" href="#" target="_blank" rel="noopener" style="display:none">Open selected website</a><small>Open this dashboard on the phone and tap the link when it appears.</small></section><section><b>Use this ESP32 as DNS</b><p>On a client connected to the same Wi-Fi network, edit its Wi-Fi settings and set DNS 1 to the ESP32 IP shown above. Turn Android Private DNS off while testing; encrypted DNS can bypass this filter.</p><p><b>Important:</b> the phone that creates a hotspot generally cannot use that same hotspot as a client. To filter that phone, connect both the ESP32 and phone to a separate router or another phone's hotspot, and ensure clients can reach each other.</p></section><section><b>Quick links</b><div id="links"></div></section></main><script>
  let lastStamp=-1;let known=[['Wikipedia','https://www.wikipedia.org/'],['DuckDuckGo','https://duckduckgo.com/'],['GitHub','https://github.com/'],['Example.com','https://example.com/']];const links=document.getElementById('links');known.forEach(x=>{let a=document.createElement('a');a.href=x[1];a.target='_blank';a.rel='noopener';a.className='open';a.textContent='Open '+x[0];links.appendChild(a)});
  async function update(){try{let r=await fetch('/status',{cache:'no-store'});let d=await r.json();document.getElementById('wifi').textContent=d.connected?'Connected · ESP32 IP '+d.ip:'Not connected to Wi-Fi';document.getElementById('stats').textContent='Queries: '+d.queries+' · Blocked: '+d.blocked+' · Forwarded: '+d.forwarded+' · Errors: '+d.errors+' · Links sent: '+d.sent;if(d.stamp!==lastStamp){lastStamp=d.stamp;document.getElementById('site').textContent=d.name;document.getElementById('url').textContent=d.url;let a=document.getElementById('open');if(d.url){a.href=d.url;a.style.display='block'}else{a.style.display='none'}}}catch(e){document.getElementById('wifi').textContent='Cannot reach ESP32. Check Wi-Fi/IP.'}}
  update();setInterval(update,1200);
  </script></body></html>)HTML";
  webServer.send(200, "text/html; charset=utf-8", html);
}

void handleWebStatus() {
  String json = "{";
  json += "\"connected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false");
  json += ",\"ip\":\"" + WiFi.localIP().toString() + "\"";
  json += ",\"name\":\"" + jsonEscape(pendingWebsiteName) + "\"";
  json += ",\"url\":\"" + jsonEscape(pendingWebsiteURL) + "\"";
  json += ",\"sent\":" + String(websitesSent);
  json += ",\"queries\":" + String(dnsQueries);
  json += ",\"blocked\":" + String(dnsBlocked);
  json += ",\"forwarded\":" + String(dnsForwarded);
  json += ",\"errors\":" + String(dnsErrors);
  json += ",\"stamp\":" + String(websitesSent);
  json += "}";
  webServer.sendHeader("Cache-Control", "no-store");
  webServer.send(200, "application/json", json);
}

void startWebServerIfConnected() {
  if (WiFi.status() == WL_CONNECTED && !webServerStarted) {
    webServer.on("/", HTTP_GET, handleWebRoot);
    webServer.on("/status", HTTP_GET, handleWebStatus);
    webServer.begin();
    webServerStarted = true;
    Serial.println("PocketOS web dashboard started.");
    Serial.print("Open dashboard: http://");
    Serial.println(WiFi.localIP());
  }
  if (WiFi.status() == WL_CONNECTED && !dnsServerStarted) {
    if (dnsUdp.begin(53) && upstreamUdp.begin(53000)) {
      dnsServerStarted = true;
      Serial.println("DNS blocker listening on UDP port 53.");
      Serial.println("Configure client DNS to the ESP32 IP shown below.");
      Serial.print("DNS server IP: ");
      Serial.println(WiFi.localIP());
    } else {
      Serial.println("Could not start DNS sockets.");
    }
  }
}

void drawWebAdBlock() {
  drawHeader("WEB ADBLOCK");
  display.setCursor(0, 12);
  display.print(WiFi.status() == WL_CONNECTED ? "WiFi/DNS: ON" : "WiFi/DNS: OFF");
  display.setCursor(0, 21);
  display.print("Blocked:");
  display.print(dnsBlocked);
  display.print(" Q:");
  display.print(dnsQueries);
  for (int i = 0; i < WEB_SITE_COUNT; i++) {
    int y = 29 + i * 6;
    if (i == webSelection) {
      display.fillRect(0, y - 1, 128, 7, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
      display.setCursor(2, y);
      display.print(">");
      display.print(webSiteNames[i]);
      display.setTextColor(invertDisplay ? SSD1306_BLACK : SSD1306_WHITE);
    } else {
      display.setCursor(2, y);
      display.print(" ");
      display.print(webSiteNames[i]);
    }
  }
  drawFooter("SEL:Send BACK:Home");
  finishDisplay();
}

void handleWebAdBlock(Button button) {
  if (button == BTN_UP) webSelection = (webSelection + WEB_SITE_COUNT - 1) % WEB_SITE_COUNT;
  if (button == BTN_DOWN) webSelection = (webSelection + 1) % WEB_SITE_COUNT;
  if (button == BTN_BACK) currentScreen = HOME;
  if (button == BTN_SELECT) {
    if (WiFi.status() != WL_CONNECTED) {
      showMessage("WEB ADBLOCK", "Connect WiFi first", 1400);
      return;
    }
    pendingWebsiteName = webSiteNames[webSelection];
    pendingWebsiteURL = webSiteURLs[webSelection];
    websitesSent++;
    showMessage("WEB ADBLOCK", "Link ready on phone", 900);
  }
}

// ---------------- CLOCK ----------------

void drawClock() {
  drawHeader("CLOCK");

  unsigned long elapsed =
    (millis() - bootMillis) / 1000;

  unsigned long hours = elapsed / 3600;
  unsigned long minutes = (elapsed / 60) % 60;
  unsigned long seconds = elapsed % 60;

  display.setTextSize(2);
  display.setCursor(8, 22);

  if (hours < 10) display.print("0");
  display.print(hours);

  display.print(":");

  if (minutes < 10) display.print("0");
  display.print(minutes);

  display.print(":");

  if (seconds < 10) display.print("0");
  display.print(seconds);

  display.setTextSize(1);
  display.setCursor(15, 44);
  display.print("UPTIME CLOCK");

  drawFooter("BACK:Home");
  finishDisplay();
}

// ---------------- SETTINGS ----------------

void drawSettings() {
  drawHeader("SETTINGS");

  display.setCursor(2, 17);

  if (settingsSelection == 0) {
    display.print(">");
  } else {
    display.print(" ");
  }

  display.print("Invert display: ");
  display.println(invertDisplay ? "ON" : "OFF");

  display.setCursor(2, 31);

  if (settingsSelection == 1) {
    display.print(">");
  } else {
    display.print(" ");
  }

  display.print("Contrast: ");
  display.println(contrastValue);

  display.setCursor(2, 45);

  if (settingsSelection == 2) {
    display.print(">");
  } else {
    display.print(" ");
  }

  display.println("Format storage");

  drawFooter("UP/DN SEL BACK");
  finishDisplay();
}

void handleSettings(Button button) {
  if (button == BTN_UP) {
    settingsSelection =
      (settingsSelection + 2) % 3;
  }

  if (button == BTN_DOWN) {
    settingsSelection =
      (settingsSelection + 1) % 3;
  }

  if (button == BTN_SELECT) {
    switch (settingsSelection) {
      case 0:
        invertDisplay = !invertDisplay;
        display.invertDisplay(invertDisplay);
        break;

      case 1:
        contrastValue += 20;

        if (contrastValue > 255) {
          contrastValue = 40;
        }

        display.ssd1306_command(
          SSD1306_SETCONTRAST
        );

        display.ssd1306_command(
          contrastValue
        );
        break;

      case 2:
        showMessage(
          "STORAGE",
          "Formatting...",
          500
        );

        if (LittleFS.format()) {
          showMessage("STORAGE", "Format complete!");
          noteLength = 0;
          noteBuffer[0] = '\0';
          noteDirty = false;
          refreshFiles();
        } else {
          showMessage("ERROR", "Format failed!");
        }
        break;
    }
  }
}

// ---------------- HOME BUTTON ----------------

void handleGlobalButtons(Button button) {
  if (button != BTN_HOME) {
    return;
  }

  // Do not discard unsaved text silently.
  if (currentScreen == NOTES && noteDirty) {
    if (saveNote()) {
      showMessage("NOTEPAD", "Note auto-saved!");
    } else {
      showMessage("ERROR", "Save failed!");
      return;
    }
  }

  currentScreen = HOME;
}

// ---------------- SETUP ----------------

void setup() {
  Serial.begin(115200);

  bootMillis = millis();

  for (int i = 0; i < 5; i++) {
    pinMode(buttons[i].pin, INPUT_PULLUP);
  }

  Wire.begin(21, 22);

  if (
    !display.begin(
      SSD1306_SWITCHCAPVCC,
      OLED_ADDRESS
    )
  ) {
    Serial.println("OLED initialization failed.");

    while (true) {
      delay(1000);
    }
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(20, 22);
  display.println("PocketOS");
  display.setCursor(15, 38);
  display.println("Starting...");
  display.display();

  if (!LittleFS.begin(false)) {
    Serial.println(
      "LittleFS mount failed. Trying format..."
    );

    if (!LittleFS.begin(true)) {
      Serial.println("LittleFS initialization failed.");
      showMessage("ERROR", "Storage unavailable", 2000);
    }
  }

  refreshFiles();

  // Connect to an existing Wi-Fi network. Devices to be filtered must be
  // on the same LAN and manually use this ESP32's IP as their DNS server.
  if (strcmp(WIFI_SSID, "YOUR_WIFI_NAME") != 0 && strlen(WIFI_SSID) > 0) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Connecting to Wi-Fi");
    unsigned long wifiStart = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 15000) {
      delay(300);
      Serial.print(".");
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("ESP32 dashboard: http://");
      Serial.println(WiFi.localIP());
      Serial.print("Set client DNS to: ");
      Serial.println(WiFi.localIP());
      startWebServerIfConnected();
    } else {
      Serial.println("Wi-Fi connection failed. Check SSID/password.");
    }
  } else {
    Serial.println("Set WIFI_SSID and WIFI_PASSWORD before using DNS blocking.");
  }

  drawHome();

  Serial.println();
  Serial.println("PocketOS started.");
  Serial.println("UP/DOWN: Navigate vertically");
  Serial.println("Hold UP/DOWN: Calculator left/right");
  Serial.println("SELECT: Open");
  Serial.println("BACK: Return/options");
  Serial.println("HOME: Home screen");
}

// ---------------- MAIN LOOP ----------------

void loop() {
  startWebServerIfConnected();
  if (webServerStarted) webServer.handleClient();
  processDnsQueries();

  Button button = readButton();

  handleGlobalButtons(button);

  switch (currentScreen) {
    case HOME:
      handleHome(button);
      drawHome();
      break;

    case NOTES:
      handleNotes(button);

      if (noteMenuOpen) {
        drawNoteMenu();
      } else {
        drawNotes();
      }
      break;

    case FILES:
      handleFiles(button);
      drawFiles();
      break;

    case CALCULATOR:
      handleCalculator(button);
      drawCalculator();
      break;

    case CLOCK_APP:
      if (button == BTN_BACK) {
        currentScreen = HOME;
      }

      drawClock();
      break;

    case SETTINGS:
      handleSettings(button);
      drawSettings();
      break;

    case WEB_ADBLOCK:
      handleWebAdBlock(button);
      drawWebAdBlock();
      break;
  }

  delay(10);
}
