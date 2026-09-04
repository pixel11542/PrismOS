#include <SPI.h>
#include <Wire.h>
#include <Arduino_GFX_Library.h>
#include <WiFiClientSecure.h>  // Required for modern HTTPS web trafficS
#include <SD.h>
#include <Preferences.h>   // <-- Add this line

// --- Photos App image decoders ---
// Install both via Arduino IDE Library Manager before compiling:
//   "JPEGDecoder" by Bodmer
//   "PNGdec" by Larry Bank (bitbank2)
#include <JPEGDecoder.h>
#include <PNGdec.h>
bool isUppercase = false;  // Tracks if Shift/Caps Lock is active

Preferences prefs;  // <-- Add this global object instance

// FIXED: Define two independent canvases with hardcoded screen coordinates
// Syntax: Arduino_Canvas(width, height, output_display, x_offset, y_offset)

// This auto-injects a corner radius of 8 into every 5-argument call
#define drawRoundRect(x, y, w, h, c) drawRoundRect(x, y, w, h, 8, c)
#define fillRoundRect(x, y, w, h, c) fillRoundRect(x, y, w, h, 8, c)

String discoveredSSIDs[5];  // Stores top 5 strongest network names
int networkCount = 0;       // Stores total networks found
bool isScanning = false;    // Prevents scanning multiple times at once

// 1. HARDWARE DISPLAY CONFIGURATION (NATIVE SPI BUS)
#define TFT_CS 5    // IO5 (LCD Screen Chip Select)
#define TFT_DC 16   // IO16 / RX2 (Standard Shield Configuration for DC)
#define TFT_RST 17  // IO17 / TX2 (Standard Shield Configuration for RESET)

// 2. TOUCH CONFIGURATION (I2C BUS)
#define TOUCH_SDA 21  // IO21
#define TOUCH_SCL 22  // IO22
#define TOUCH_RST 25  // IO25
#define TOUCH_INT 26  // IO26
#define FT6X36_ADDR 0x38

// 3. SD CARD CONFIGURATION
#define SD_CS_PIN 4  // IO4 (SD Card Reader Chip Select)
// FIXED: Changed peripheral number to VSPI so DMA routes to your physical pins (18, 23, 19)
Arduino_DataBus *bus = new Arduino_ESP32SPI(TFT_DC, TFT_CS, 18, 23, 19, VSPI);
Arduino_ST7796 *tft = new Arduino_ST7796(bus, TFT_RST, 0, false);

// FIXED: Declare two separate canvases with pre-mapped coordinates
// Syntax: Arduino_Canvas(width, height, output_display, x_offset, y_offset)
Arduino_Canvas *notepadCanvas = new Arduino_Canvas(300, 50, tft, 10, 45);
Arduino_Canvas *terminalCanvas = new Arduino_Canvas(300, 50, tft, 10, 230);

// FIXED: Create a fast, lightweight sub-canvas (300 width x 50 height)
// Requires only 30KB of RAM! Used exclusively to update typing strings and text inputs.
Arduino_Canvas *inputCanvas = new Arduino_Canvas(300, 50, tft);

// Explicitly pass DMA Channel 1 (or 2) into the HWSPI constructor argument
// Syntax: Arduino_HWSPI(rt_dc, rt_cs, rt_sck, rt_mosi, rt_miso, dma_channel)
// CHANGE THIS: Remove the ', 1' argument that is breaking your compile


// --- Native ESP32 I2C Pins for CTP (Capacitive Touch Panel) ---
#define CTP_SDA_PIN 21  // Hardware SDA Pin on D1 R32

// 16-Bit Hex Color Constants
#define COLOR_BLACK 0x0000
#define COLOR_WHITE 0xFFFF
#define COLOR_DARKGREY 0x4208
#define COLOR_LIGHTGREY 0xC618
#define COLOR_BLUE 0x001F
#define COLOR_GREEN 0x07E0
#define COLOR_RED 0xF800
#define COLOR_ORANGE 0xFDA0
#define COLOR_GREY 0x7BEF
#define COLOR_YELLOW 0xFFE0
#define COLOR_PURPLE 0x780F


// Interface Prototyping Forward Declarations
void showLockScreen();
void showHomeScreen();
void showCalculatorScreen();
void showSettingsScreen();
void updatePinDots();
void updateCalcDisplay();
void updateSettingsToggles();
void gotoUnlockGateway();
bool checkLockdownActive();
void setHardwareScrollRow(uint16_t row);
void handleLockTouch(int x, int y);
void handleLockTouchRelease();
void slideDownLockScreen(int startY);
void handleHomeTouch(int x, int y);
void handleCalcTouch(int x, int y);
void handleSettingsTouch(int x, int y);
void executeInstantUnlock();
void readSDCardFiles();
void drawQWERTYKeyboard();
void showNotesScreen();
void showTerminalScreen();
void initGoogleScreen();

// --- Paint (Drawing) App ---
void showDrawScreen();
void handleDrawTouch(int x, int y);
void drawPaintToolbar();
void redrawDrawingFromPoints();
void clearDrawingCanvas();
void saveDrawingToSD();
void loadDrawingFromSD(String filename);
void recordDrawPoint(int x, int y, uint16_t color, uint8_t size, bool newStroke);
void drawInterpolatedLine(int x0, int y0, int x1, int y1, uint16_t color, uint8_t size);
void drawPaintIcon(int x, int y);

// --- Photos (Image Viewer) App ---
void showPhotosGallery();
void handlePhotosTouch(int x, int y);
void showImageFullscreen(String filename);
void drawPhotosIcon(int x, int y);
bool drawBmpFromSD(String path, int destX, int destY);
bool drawJpegFromSD(String path, int destX, int destY);
bool drawPngFromSD(String path, int destX, int destY);
bool isImageFileName(String name);



String currentOpenedFileName = "";  // Tracks the active file name for saving/resaving

#include "image_data.h"

#define TFT_BL 14  // Defines your screen backlight pin
int pipeX = 320;
int pipeGapY = 200;
const int pipeWidth = 40;
const int pipeGapHeight = 100;
int flappyScore = 0;
int snakeX[20] = { 160, 150, 140 };
int snakeY[20] = { 240, 240, 240 };
int snakeLength = 3;
int snakeDir = 1;  // 0=Up, 1=Right, 2=Down, 3=Left
int foodX = 100;
int foodY = 100;
struct FileItem {
  String name;
  bool isDirectory;
};

FileItem fileList[20];  // Limits view to 20 items to conserve memory
int totalFiles = 0;
int scrollOffset = 0;  // For future scroll bar handling

String calcInput = "";        // Stores current entry string
double firstNum = 0;          // Stores first operand
char calcOp = ' ';            // Tracks active operator (+, -, *, /)
bool clearOnNextKey = false;  // Automatically wipes previous result on new entry

enum SetupPage {
  SETUP_WELCOME,
  SETUP_WIFI,
  SETUP_LOCKSTYLE,
  SETUP_COMPLETE
};
SetupPage currentSetupPage = SETUP_WELCOME;
bool isSetupWizardComplete = false;  // Set to true after completing the flow



enum PhoneState { STATE_LOCKED,
                  STATE_HOME,
                  STATE_CALC,
                  STATE_SETTINGS,
                  STATE_GAMES,
                  STATE_SNAKE,
                  STATE_FLAPPY,
                  STATE_PONG,
                  STATE_FILES,
                  STATE_SETUP,
                  STATE_DRAW,
                  STATE_PHOTOS };
PhoneState currentScreen = STATE_LOCKED;
enum LockStyle { LOCK_SLIDE_UP,
                 LOCK_SLIDE_RIGHT,
                 LOCK_PIN,
                 LOCK_TAP,
                 LOCK_NONE };
LockStyle currentLockStyle = LOCK_SLIDE_UP;

bool pinRequired = false;
String enteredPin = "";
String currentPasscode = "1234";
int wrongAttempts = 0;
unsigned long lockUntilTime = 0;
int baseTimeoutSeconds = 30;
int touchStartPos = -1;
bool isSliding = false;

// --- Global Keyboard & Wi-Fi Settings ---
String selectedSSID = "";
String typingPassword = "";
int currentKeyboardPage = 0;  // 0 = Letters, 1 = Numbers/Symbols
bool wifiEnabled = true;
bool bluetoothEnabled = false;


#include <WiFi.h>
#include <HTTPClient.h>
#include "time.h"

// CLOCK CONFIGURATION PARAMETERS
const char *ntpServer = "pool.ntp.org";
// Adjust the GMT offset to match your local timezone in seconds
// (e.g., Eastern Time GMT -5 = -18000 | Pacific Time GMT -8 = -28800)
const long gmtOffset_sec = -18000;
const int daylightOffset_sec = 3600;  // Set to 3600 if Daylight Saving Time is active, otherwise 0

// INTERNET CONNECTION CREDENTIALS
const char *WIFI_SSID = "scarface";
const char *WIFI_PASS = "2kSuncruiser!";

// APPLICATION STORAGE BUFFERS
String notesText = "";
String googleQuery = "";
String googleResult = "";

enum SubApps { APP_NONE,
               APP_NOTES,
               APP_GOOGLE,
               APP_TERMINAL };
SubApps activeSubApp = APP_NONE;

String terminalInput = "";
String terminalResponse = "System Terminal Initialized.\nType HELP for commands.";

// =========================================================================
// PAINT (DRAWING) APP STATE
// The canvas itself is never kept as a full pixel buffer in RAM (a 320x480
// 16-bit framebuffer would be ~300KB, more than this board's free heap).
// Instead every stroke is recorded as a lightweight list of points, which
// is cheap to store, cheap to save/load to SD, and easy to replay to redraw
// the canvas after loading a saved drawing.
// =========================================================================
#define MAX_DRAW_POINTS 1500
struct DrawPoint {
  int16_t x;
  int16_t y;
  uint16_t color;
  uint8_t size;
  bool newStroke;  // true = start of a new stroke (don't connect to prev point)
};
DrawPoint drawPoints[MAX_DRAW_POINTS];
int drawPointCount = 0;

uint16_t currentDrawColor = COLOR_BLACK;
uint8_t currentBrushSize = 6;   // Medium by default
bool eraserActive = false;
int lastDrawX = -1;
int lastDrawY = -1;
String currentDrawingFileName = "";

const uint16_t paintPalette[8] = { COLOR_BLACK, COLOR_WHITE, COLOR_RED, COLOR_ORANGE,
                                    COLOR_YELLOW, COLOR_GREEN, COLOR_BLUE, COLOR_PURPLE };

// =========================================================================
// PHOTOS (IMAGE VIEWER) APP STATE
// =========================================================================
bool photosOpenedFromFiles = false;   // Where BACK should return to
bool viewingFullscreenImage = false;  // Gallery list vs. fullscreen viewer
String currentImageFileName = "";
// --- HARDWARE WATCHDOG PANIC DRIVER ---
unsigned long loopWatchdogTimeout = 0;
bool isPanicActive = false;

void triggerRedScreenPanic(String errorMessage) {
  isPanicActive = true;

  // Force reset hardware transaction locks on the bus
  tft->endWrite();

  // Fill the physical panel with a pure emergency warning blue
  tft->fillScreen(COLOR_BLUE);

  tft->setTextColor(COLOR_WHITE);
  tft->setTextSize(3);
  tft->setCursor(20, 60);
  tft->print(":(");

  tft->setTextSize(2);
  tft->setCursor(20, 120);
  tft->print("Your device ran into a                problem");

  tft->setTextSize(1);
  tft->setCursor(20, 160);
  tft->print(errorMessage);

  tft->setTextSize(2);
  tft->setCursor(20, 360);
  tft->print("Press Reset Button");
  tft->setCursor(20, 390);
  tft->print("to restart node...");

  // Permanently loop right here to completely freeze the system safely in the error view
  while (true) {
    delay(100);
  }
}

void handleTerminalTouch(int x, int y) {
  // 1. Exit Check: Return Home if status bar is pressed
  if (y < 35) {
    activeSubApp = APP_NONE;
    currentScreen = STATE_HOME;
    showHomeScreen();
    delay(300);
    return;
  }

  // 2. Green "RUN" Button Check (Y = 420 to 460, X = 225 to 305)
  if (y >= 420 && y <= 460 && x >= 225 && x <= 305) {
    if (activeSubApp == APP_TERMINAL) {
      processTerminalCommand(terminalInput);  // Fire engine parser
    }
    return;
  }
}

bool lastButtonState = HIGH;  // Tracks previous button state
bool pinState = LOW;

void powerButtonCheck() {
  int currentButtonState = digitalRead(27);

  // Detect transition from unpressed (HIGH) to pressed (LOW)
  if (currentButtonState == LOW && lastButtonState == HIGH) {
    delay(50);  // Simple software debounce delay to filter electrical noise

    pinState = !pinState;        // Flip the boolean state (true becomes false, false becomes true)
    digitalWrite(17, pinState);  // Apply the new state directly to the physical pin
  }

  lastButtonState = currentButtonState;  // Save current state for the next cycle
}

void scanForNetworks() {
  if (isScanning) return;  // Exit if a scan is already running
  isScanning = true;

  Serial.println("Scanning for networks...");
  // Clear old entries
  for (int i = 0; i < 5; i++) discoveredSSIDs[i] = "";

  // Trigger synchronous scan (returns number of networks found)
  int found = WiFi.scanNetworks();
  networkCount = (found > 5) ? 5 : found;  // Cap at 5 networks to fit the screen

  for (int i = 0; i < networkCount; i++) {
    discoveredSSIDs[i] = WiFi.SSID(i);  // Save network name
  }

  WiFi.scanDelete();  // Free memory used by the scan
  isScanning = false;
  Serial.println("Scan complete.");
}


void processTerminalCommand(String cmd) {
  cmd.trim();
  cmd.toUpperCase();  // Force uppercase evaluations

  if (cmd == "HELP") {
    terminalResponse = "Commands:\n- HELP: Show commands\n- CLEAR: Clear log screen\n- FILES: Count SD files\n- SYSTEM: System profile\n- REBOOT: Restart ESP32";
  } else if (cmd == "CLEAR") {
    terminalResponse = "";
  } else if (cmd == "FILES") {
    // Counts items inside root partition
    int count = 0;
    File root = SD.open("/");
    if (root) {
      File entry = root.openNextFile();
      while (entry) {
        count++;
        entry.close();
        entry = root.openNextFile();
      }
      root.close();
      terminalResponse = "SD Root Evaluation:\nTotal objects found: " + String(count);
    } else {
      terminalResponse = "Storage Drive Error: Mount Failed.";
    }
  } else if (cmd == "SYSTEM") {
    terminalResponse = "OS Matrix Node profile:\nChip: ESP32 / Wemos R32\nCPU Speed: " + String(ESP.getCpuFreqMHz()) + " MHz\nFree Heap: " + String(ESP.getFreeHeap()) + " Bytes";
  } else if (cmd == "REBOOT") {
    tft->fillScreen(COLOR_BLACK);
    tft->setCursor(100, 240);
    tft->print("Rebooting...");
    delay(1000);
    ESP.restart();
  } else if (cmd == "") {
    terminalResponse = "Empty prompt line.";
  } else {
    terminalResponse = "Unknown syntax error:\n'" + cmd + "' has not been found.";
  }

  terminalInput = "";    // Wipe active keyboard string buffer clean after running command
  showTerminalScreen();  // Re-render terminal frames
}

void renderSetupButton(int y, String label, uint16_t bgColor) {
  tft->fillRoundRect(15, y, 290, 45, bgColor);
  tft->drawRoundRect(15, y, 290, 45, COLOR_WHITE);
  tft->setTextColor(COLOR_BLACK);
  tft->setTextSize(2);
  // Simple centering text approximation offset calculation
  int textX = 160 - (label.length() * 6);
  tft->setCursor(textX, y + 14);
  tft->print(label);
}

void showSetupWizard() {
  tft->fillScreen(COLOR_WHITE);
  tft->setTextColor(COLOR_BLACK);

  if (currentSetupPage == SETUP_WELCOME) {
    tft->setTextSize(3);
    tft->setCursor(30, 80);
    tft->print("Welcome to");
    tft->setCursor(30, 120);
    tft->setTextColor(COLOR_GREEN);
    tft->print("PhoneOS");

    tft->setTextColor(COLOR_LIGHTGREY);
    tft->setTextSize(1);
    tft->setCursor(30, 180);
    tft->print("Let's configure your handheld device.");
    tft->setCursor(30, 195);
    tft->print("Tap below to begin initialization.");

    renderSetupButton(360, "GET STARTED", COLOR_GREEN);
  } else if (currentSetupPage == SETUP_WIFI) {
    tft->setTextSize(2);
    tft->setCursor(20, 40);
    tft->print("Connect to Wi-Fi");

    // Mock wireless station target options
    renderSetupButton(110, wifiEnabled ? "Wi-Fi Network [CONNECTED]" : "Home_Router_2.4G", COLOR_GREY);
    renderSetupButton(170, "Public_Hotspot_Open", COLOR_GREY);
    renderSetupButton(230, "Office_Secure_5G", COLOR_GREY);

    renderSetupButton(360, "SKIP CONNECTION", COLOR_LIGHTGREY);
  } else if (currentSetupPage == SETUP_LOCKSTYLE) {
    tft->setTextSize(2);
    tft->setCursor(20, 40);
    tft->print("Choose Lock Type");

    // Provide immediate physical lock configurations options
    renderSetupButton(110, (currentLockStyle == LOCK_PIN) ? "-> PIN PAD [SELECTED]" : "Use PIN Code", COLOR_GREY);
    renderSetupButton(170, (currentLockStyle == LOCK_SLIDE_UP) ? "-> SLIDE [SELECTED]" : "Use Slide Gesture", COLOR_GREY);
    renderSetupButton(230, (currentLockStyle == LOCK_NONE) ? "-> NO LOCK [SELECTED]" : "No Lock Screen", COLOR_GREY);

    renderSetupButton(360, "NEXT STEP", COLOR_GREEN);
  } else if (currentSetupPage == SETUP_COMPLETE) {
    tft->setTextSize(3);
    tft->setCursor(30, 100);
    tft->setTextColor(COLOR_GREEN);
    tft->print("All Set!");

    tft->setTextColor(COLOR_WHITE);
    tft->setTextSize(2);
    tft->setCursor(30, 160);
    tft->print("Your phone is");
    tft->setCursor(30, 190);
    tft->print("ready to use.");

    renderSetupButton(360, "FINISH SETUP", COLOR_GREEN);
  }
}

void handleSetupTouch(int x, int y) {
  // 1. WELCOME PAGE INTERRUPTS
  if (currentSetupPage == SETUP_WELCOME) {
    if (x >= 15 && x <= 305 && y >= 360 && y <= 405) {
      currentSetupPage = SETUP_WIFI;
      showSetupWizard();
      delay(250);
    }
  }
  // 2. WI-FI SCREEN SELECTIONS
  else if (currentSetupPage == SETUP_WIFI) {
    if (x >= 15 && x <= 305) {
      if (y >= 110 && y <= 155) {
        wifiEnabled = true;
        showSetupWizard();
      } else if (y >= 170 && y <= 215) {
        wifiEnabled = true;
        showSetupWizard();
      } else if (y >= 230 && y <= 275) {
        wifiEnabled = true;
        showSetupWizard();
      } else if (y >= 360 && y <= 405) {  // Skip Button
        currentSetupPage = SETUP_LOCKSTYLE;
        showSetupWizard();
      }
      delay(200);
    }
  }
  // 3. SECURITY STEP CHANGER
  else if (currentSetupPage == SETUP_LOCKSTYLE) {
    if (x >= 15 && x <= 305) {
      if (y >= 110 && y <= 155) {
        currentLockStyle = LOCK_PIN;
        pinRequired = true;
        showSetupWizard();
      } else if (y >= 170 && y <= 215) {
        currentLockStyle = LOCK_SLIDE_UP;
        pinRequired = false;
        showSetupWizard();
      } else if (y >= 230 && y <= 275) {
        currentLockStyle = LOCK_NONE;
        pinRequired = false;
        showSetupWizard();
      } else if (y >= 360 && y <= 405) {  // Next Button
        currentSetupPage = SETUP_COMPLETE;
        showSetupWizard();
      }
      delay(200);
    }
  }
  // 4. DESTINATION ROUTER
  else if (currentSetupPage == SETUP_COMPLETE) {
    if (x >= 15 && x <= 305 && y >= 360 && y <= 405) {

      // --- SAVE COMPLETED STATE TO FLASH ---
      prefs.begin("phone_sys", false);
      prefs.putBool("setup_done", true);  // Overwrite false with true permanently
      prefs.end();

      isSetupWizardComplete = true;
      currentScreen = STATE_HOME;
      // Unlock device to home app grid launcher
      showHomeScreen();
      delay(300);
    }
  }
}


void showTerminalScreen() {
  drawTopStatusBar();
  activeSubApp = APP_TERMINAL;

  int winX = 10;
  int winY = 35;
  int winW = 300;
  int winH = 220;
  uint16_t bgColor = 0x10A2;

  // FIXED: Replaced raw address window write with the library's safe hardware fill
  tft->fillRect(winX, winY, winW, winH, bgColor);

  // Overlay graphics and text safely on top
  tft->drawRoundRect(winX, winY, winW, winH, COLOR_WHITE);

  tft->setTextColor(COLOR_GREEN);
  tft->setTextSize(1);
  tft->setCursor(15, 45);
  tft->print(terminalResponse);

  tft->setCursor(15, 235);
  tft->print("> " + terminalInput + "_");

  drawQWERTYKeyboard();
}







// REAL-TIME NETWORK SEARCH ENGINE


void executeGoogleSearch(String query) {
  // 1. Instantly display loading state to user inside the results card box area
  tft->fillRoundRect(10, 95, 300, 155, COLOR_DARKGREY);
  tft->setTextColor(COLOR_WHITE);
  tft->setTextSize(1);
  tft->setCursor(15, 105);
  tft->print("Querying Google web routes...");

  // Safety Check: Verify active wireless network connection link before executing
  if (WiFi.status() != WL_CONNECTED) {
    tft->fillRoundRect(10, 95, 300, 155, COLOR_DARKGREY);
    tft->setTextColor(COLOR_RED);
    tft->setCursor(15, 120);
    tft->print("Error: No Wi-Fi Link.");
    tft->setCursor(15, 135);
    tft->print("Check phone network config.");
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();  // Skips heavy SSL certificate verification checks to save RAM

  // Clean isolated API domain name host target profile
  const char *host = "en.wikipedia.org";
  query.trim();

  // Format space characters to HTML URL encoding format (%20) so the web server can read it
  String encodedQuery = "";
  for (int i = 0; i < query.length(); i++) {
    if (query[i] == ' ') encodedQuery += "%20";
    else encodedQuery += query[i];
  }

  Serial.println("Querying google for: " + query);

  // Establish connection on secure HTTPS Port 443
  if (!client.connect(host, 443)) {
    googleResult = "Web Connection Failed.\nServer unreachable.";
  } else {
    // Build Wikipedia Text Extract Endpoint request string
    // excharts=2 limits sentences, exintro=1 grabs summary, explaintext=1 removes raw HTML formatting tags
    String url = "/w/api.php?action=query&prop=extracts&exintro=1&explaintext=1&exchars=200&format=json&titles=" + encodedQuery;

    // Send authenticated HTTP 1.1 request identifying the Wemos board
    client.print(String("GET ") + url + " HTTP/1.1\r\n" + "Host: " + host + "\r\n" + "User-Agent: ESP32_Phone_OS/1.0 (Wemos_D1_R32)\r\n" + "Accept: application/json\r\n" + "Connection: close\r\n\r\n");

    // Process secure packet stream responses with a strict 2.5-second safety timeout limit
    long startTime = millis();
    while (!client.available() && (millis() - startTime < 2500)) {
      delay(10);
    }

    if (millis() - startTime >= 2500) {
      googleResult = "Network error:\nResponse timeout limit reached.";
    } else {
      // Skip raw HTTP response headers completely to isolate the raw JSON text body payload
      if (client.find("\r\n\r\n")) {
        // Scan the network buffer stream byte-by-byte for the specific text extract signature
        // Wikipedia puts plain text summary data behind the '"extract":"' structural key marker
        if (client.find("\"extract\":\"")) {
          googleResult = "";

          // Stream characters directly into our result text string array until reaching the closing quote
          // Set to 150 characters maximum to fit nicely within your 300x155 tft display limits
          while (client.available() && googleResult.length() < 150) {
            char c = client.read();
            if (c == '"') break;  // Closing quote mark signals a complete extract string

            // Handle basic JSON escaped backslash characters inline on the fly
            if (c == '\\') {
              if (client.available()) {
                char nextC = client.read();
                if (nextC == '/') googleResult += '/';
                else if (nextC == '"') googleResult += '"';
                else if (nextC == 'n') googleResult += '\n';  // Map literal JSON \n strings directly to actual layout breaks
              }
            } else {
              googleResult += c;
            }
          }

          // Fallback message handles blank entry lookup spaces
          if (googleResult == "") {
            googleResult = "No instant text summary available.\nTry an encyclopedic topic.";
          }
        } else {
          googleResult = "Article page not found.\nCheck spelling / exact terms.";
        }
      } else {
        googleResult = "Error: Invalid packet response.";
      }
    }
  }
  client.stop();  // Safely disconnect sockets and release shared memory buffers

  // 4. Force-render your live Wikipedia text abstract response data directly onto the canvas panel cards
  tft->fillRoundRect(10, 95, 300, 155, COLOR_DARKGREY);
  tft->setTextColor(COLOR_WHITE);
  tft->setTextSize(1);

  // Render search context descriptor line in yellow
  tft->setCursor(15, 105);
  tft->setTextColor(0xFFE0);
  tft->print("Wiki Results for: " + query);

  // Render clean fetched data block inside white layout row limits
  tft->setCursor(15, 125);
  tft->setTextColor(COLOR_WHITE);
  tft->print(googleResult);
}






void synchronizeInternalClock() {
  Serial.println("[TIME] Syncing internal clocks with pool.ntp.org...");

  // Configures the structural ESP32 system clock registers automatically
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  // Wait a brief moment for the internet packets to settle
  struct tm timeinfo;
  int retry = 0;
  while (!getLocalTime(&timeinfo) && retry < 10) {
    delay(200);
    retry++;
  }
}

void drawTopStatusBar() {
  tft->fillRoundRect(0, 0, 320, 24, COLOR_DARKGREY);
  tft->setTextColor(COLOR_WHITE);
  tft->setTextSize(1);

  struct tm timeinfo;
  String timeString = "--:--";

  // Read the native hardware RTC registers synchronized by the background network pool
  if (getLocalTime(&timeinfo)) {
    char timeHourMin[6];  // FIXED BUFFER ARRAY SIZE: Safely holds 5 characters plus terminal null '\0'
    strftime(timeHourMin, sizeof(timeHourMin), "%H:%M", &timeinfo);
    timeString = String(timeHourMin);
  }

  // Prints your live real-world time right to the center axis panel marker
  tft->setCursor(145, 6);
  tft->print(timeString);

  tft->setCursor(10, 6);
  tft->print("4G LTE");
  tft->setCursor(275, 6);
  tft->print("100%");

  if (currentScreen == STATE_CALC || currentScreen == STATE_SETTINGS) {
    tft->fillRoundRect(0, 0, 60, 24, COLOR_ORANGE);
    tft->setCursor(15, 6);
    tft->setTextColor(COLOR_BLACK);
    tft->print("< BACK");
  }
}


void updatePinDots() {
  tft->fillRoundRect(90, 145, 140, 25, COLOR_DARKGREY);
  tft->setTextColor(COLOR_WHITE);
  tft->setTextSize(2);
  tft->setCursor(100, 150);
  for (int i = 0; i < 4; i++) {
    if (i < enteredPin.length()) tft->print("* ");
    else tft->print("- ");
  }
}



void handleSnakeTouch(int x, int y) {
  // 1. Back button check (Go home if pressed near top-left 0,0 origin)
  if (x >= 0 && x <= 60 && y >= 0 && y <= 24) {
    currentScreen = STATE_HOME;
    showHomeScreen();
    delay(300);
    return;
  }

  // 2. Vector D-pad math relative to screen center (160, 240)
  int dx = x - 160;
  int dy = y - 240;

  if (abs(dx) > abs(dy)) {
    // Horizontal movements (Left/Right)
    if (dx > 0 && snakeDir != 3) {
      snakeDir = 1;  // Turn Right (Prevent turning directly back into yourself)
    } else if (dx < 0 && snakeDir != 1) {
      snakeDir = 3;  // Turn Left
    }
  } else {
    // Vertical movements (Up/Down)
    if (dy > 0 && snakeDir != 0) {
      snakeDir = 2;  // Turn Down
    } else if (dy < 0 && snakeDir != 2) {
      snakeDir = 0;  // Turn Up
    }
  }
}


int birdY = 240;
int birdVelocity = 0;
int ballX = 160;
int ballY = 240;
int ballDX = 3;
int ballDY = 3;
int paddleX = 120;
const int paddleWidth = 60;

void handleFlappyTouch(int x, int y) {
  // Any tap anywhere on the screen makes the bird flap up!
  if (x >= 0 && x <= 60 && y >= 0 && y <= 24) {
    currentScreen = STATE_HOME;
    showHomeScreen();
    delay(300);
    return;
  }
  birdVelocity = -6;
}

void handlePongTouch(int x, int y) {
  // Map the touch X coordinate directly to your paddle position
  if (x >= 0 && x <= 60 && y >= 0 && y <= 24) {
    currentScreen = STATE_HOME;
    showHomeScreen();
    delay(300);
    return;
  }
  paddleX = x - (paddleWidth / 2);
}


void showGamesScreen() {
  drawTopStatusBar();

  // Draws the rectangles/buttons
  tft->fillRoundRect(40, 80, 80, 80, COLOR_RED);

  tft->setCursor(68, 112);
  tft->print("[snake]");
  tft->setCursor(40, 170);
  tft->print("Snake");

  tft->fillRoundRect(200, 80, 80, 80, COLOR_ORANGE);

  tft->setCursor(222, 112);
  tft->print("[bird]");
  tft->setCursor(205, 170);
  tft->print("Flappy Bird");

  tft->fillRoundRect(40, 230, 80, 80, COLOR_BLUE);

  tft->setCursor(65, 262);
  tft->print("[ball]");
  tft->setCursor(40, 320);
  tft->print("Pong");

  tft->fillRoundRect(200, 230, 80, 80, COLOR_GREEN);

  tft->setCursor(225, 262);
  tft->print("placeholder");
  tft->setCursor(200, 320);
  tft->print("placeholder");
}

// --- GLOBAL GAME VARIABLES (Put these at the very top of your file) ---


// --- FLAPPY BIRD ENGINE ---
void startFlappyBird() {
  tft->fillScreen(COLOR_BLACK);
  drawTopStatusBar();
  birdY = 240;
  birdVelocity = 0;
}
void updateFlappyBird() {
  tft->fillRect(140, birdY, 20, 20, COLOR_BLACK);

  int wipeX = pipeX + pipeWidth;
  // Keep strict bounds tracking but use official library primitive
  if (wipeX >= 0 && wipeX <= (320 - 4)) {
    tft->fillRect(wipeX, 30, 4, 450, COLOR_BLACK);
  }

  birdVelocity += 1;
  birdY += birdVelocity;
  pipeX -= 4;

  if (pipeX < -pipeWidth) {
    pipeX = 320;
    pipeGapY = random(80, 300);
    flappyScore++;
    tft->fillRect(0, 30, 320, 450, COLOR_BLACK);
  }

  if (birdY > 440 || birdY < 30) {
    startFlappyBird();
    return;
  }
  if (pipeX < 160 && (pipeX + pipeWidth) > 140) {
    if (birdY < pipeGapY || (birdY + 20) > (pipeGapY + pipeGapHeight)) {
      startFlappyBird();
      return;
    }
  }

  tft->fillRect(pipeX, 30, pipeWidth, pipeGapY - 30, COLOR_GREEN);
  tft->fillRect(pipeX, pipeGapY + pipeGapHeight, pipeWidth, 480 - (pipeGapY + pipeGapHeight), COLOR_GREEN);
  tft->fillRect(140, birdY, 20, 20, COLOR_ORANGE);

  delay(35);
}




// --- PONG ENGINE ---
void startPongGame() {
  tft->fillScreen(COLOR_BLACK);
  drawTopStatusBar();
  ballX = 160;
  ballY = 240;
}

void updatePongGame() {
  // Clear old objects
  tft->fillRoundRect(ballX, ballY, 10, 10, COLOR_BLACK);
  tft->fillRoundRect(0, 440, 320, 10, COLOR_BLACK);  // Clear paddle area

  // Move ball
  ballX += ballDX;
  ballY += ballDY;

  // Wall collisions
  if (ballX <= 0 || ballX >= 310) ballDX = -ballDX;
  if (ballY <= 30) ballDY = -ballDY;

  // Paddle collision
  if (ballY >= 430 && ballX >= paddleX && ballX <= (paddleX + paddleWidth)) {
    ballDY = -ballDY;
  }

  // Reset if missed
  if (ballY > 480) {
    ballX = 160;
    ballY = 240;
  }

  // Draw objects
  tft->fillRoundRect(ballX, ballY, 10, 10, 0xFFE0);                // Yellow Ball
  tft->fillRoundRect(paddleX, 440, paddleWidth, 10, COLOR_WHITE);  // Paddle
  delay(16);
}

void startSnakeGame() {
  tft->fillScreen(COLOR_BLACK);
  drawTopStatusBar();
  snakeLength = 3;
  snakeDir = 1;
  snakeX[0] = 160;
  snakeY[0] = 240;
  foodX = random(2, 30) * 10;
  foodY = random(4, 45) * 10;
}

void updateSnakeGame() {
  // 1. Erase tail tip from previous frame position
  tft->fillRoundRect(snakeX[snakeLength - 1], snakeY[snakeLength - 1], 10, 10, COLOR_BLACK);

  // 2. Shift body coordinates down array chain
  for (int i = snakeLength - 1; i > 0; i--) {
    snakeX[i] = snakeX[i - 1];
    snakeY[i] = snakeY[i - 1];
  }

  // 3. Move head based on direction variable
  if (snakeDir == 0) snakeY[0] -= 10;
  else if (snakeDir == 1) snakeX[0] += 10;
  else if (snakeDir == 2) snakeY[0] += 10;
  else if (snakeDir == 3) snakeX[0] -= 10;

  // 4. Wall or Self collision check
  if (snakeX[0] < 0 || snakeX[0] >= 320 || snakeY[0] < 30 || snakeY[0] >= 480) {
    startSnakeGame();
    return;
  }

  // 5. Food item collision check
  if (snakeX[0] == foodX && snakeY[0] == foodY) {
    if (snakeLength < 20) snakeLength++;
    foodX = random(2, 30) * 10;
    foodY = random(4, 45) * 10;
  }

  // 6. Render game elements onto matrix
  tft->fillRoundRect(foodX, foodY, 10, 10, COLOR_RED);            // Draw target food
  tft->fillRoundRect(snakeX[0], snakeY[0], 10, 10, COLOR_GREEN);  // Draw head node

  delay(100);  // Controls game difficulty speed
}




void handleGamesTouch(int x, int y) {
  drawTopStatusBar();
  if (x >= 0 && x <= 60 && y >= 0 && y <= 24) {
    currentScreen = STATE_HOME;
    showHomeScreen();
    delay(300);
    return;
  }
  // Check for Snake (Row 1, Left)
  if (x >= 40 && x <= 120 && y >= 80 && y <= 160) {
    currentScreen = STATE_SNAKE;
    startSnakeGame();
    delay(300);
  }
  // Check for Flappy Bird (Row 1, Right)
  else if (x >= 200 && x <= 280 && y >= 80 && y <= 160) {
    currentScreen = STATE_FLAPPY;
    startFlappyBird();
    delay(300);
  }
  // Check for Pong (Row 2, Left)
  else if (x >= 40 && x <= 120 && y >= 230 && y <= 310) {
    currentScreen = STATE_PONG;
    startPongGame();
    delay(300);
  }
  // Check for Placeholder Game (Row 2, Right)
  else if (x >= 200 && x <= 280 && y >= 230 && y <= 310) {
    // Optional: add a 4th game later
  }
}


void playModernOpenAnimation(int startX, int startY, uint16_t appBgColor) {
  int cx = startX + 30;  // Find the exact center X of your 60px app icon
  int cy = startY + 30;  // Find the exact center Y of your 60px app icon

  int px = cx, py = cy, pw = 0, ph = 0;

  int size = 4;      // Initial starting height of the zoom square seed
  int velocity = 2;  // Starting exponential expansion step velocity

  // Track height scaling all the way up to the full 480 vertical pixel ceiling
  while (size <= 480) {
    int h = size;
    int w = (h * 320) / 480;  // Corrected 2:3 vertical scaling for 320x480 resolution panels

    int x = cx - (w / 2);
    int y = cy - (h / 2);

    // HARD SCREEN CLIPPING: Locks boundaries directly to the 320x480 physical glass limits
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x + w > 320) w = 320 - x;
    if (y + h > 480) h = 480 - y;

    // SPI Edge Expansion: Only paint new outer perimeter strips to stay fast and smooth
    if (size > 4) {
      tft->fillRect(x, y, w, py - y, appBgColor);                       // Top expanded border
      tft->fillRect(x, py + ph, w, (y + h) - (py + ph), appBgColor);    // Bottom expanded border
      tft->fillRect(x, py, px - x, ph, appBgColor);                     // Left expanded border
      tft->fillRect(px + pw, py, (x + w) - (px + pw), ph, appBgColor);  // Right expanded border
    } else {
      tft->fillRect(x, y, w, h, appBgColor);
    }

    px = x;
    py = y;
    pw = w;
    ph = h;

    // Force final loop cycle step to align flat against the screen limits
    if (size == 480) break;
    size += velocity;
    velocity += 4;  // Compounding acceleration step factor
    if (size > 480) size = 480;

    delay(4);  // Short hardware pacing delay loop guard
  }

  // Wipe screen clean with native app color to guarantee a clean canvas
  tft->fillScreen(appBgColor);
}





void playModernCloseAnimation(uint16_t appBgColor) {
  // Smoothly collapse inward continuously from 320 down to 1
  for (int size = 320; size >= 1; size -= 4) {
    int w = size;
    int h = (w * 240) / 320;

    int x = 160 - (w / 2);
    int y = 120 - (h / 2);

    // Fast border wipes to swallow the window pixels cleanly
    tft->fillRect(0, 0, 320, y, COLOR_BLACK);
    tft->fillRect(0, y + h, 320, 240 - (y + h), COLOR_BLACK);
    tft->fillRect(0, y, x, h, COLOR_BLACK);
    tft->fillRect(x + w, y, 320 - (x + w), h, COLOR_BLACK);
  }
  tft->fillScreen(COLOR_BLACK);
}





void drawGamesIcon(int x, int y) {
  tft->fillRoundRect(x, y, 60, 60, 0x780F);
  tft->fillRoundRect(x + 12, y + 21, 36, 18, COLOR_WHITE);
  tft->fillRect(x + 16, y + 29, 9, 3, 0x780F);
  tft->fillRect(x + 19, y + 26, 3, 9, 0x780F);
  tft->fillCircle(x + 36, y + 30, 3, COLOR_RED);
  tft->fillCircle(x + 42, y + 30, 3, COLOR_YELLOW);
}

void drawFilesIcon(int x, int y) {
  tft->fillRoundRect(x, y, 60, 60, 0x03EF);
  tft->fillRoundRect(x + 12, y + 18, 18, 6, COLOR_YELLOW);
  tft->fillRect(x + 12, y + 22, 36, 22, COLOR_YELLOW);
  tft->fillRect(x + 20, y + 36, 20, 10, COLOR_BLUE);
}

void drawNotesIcon(int x, int y) {
  tft->fillRoundRect(x, y, 60, 60, 0xFBE0);
  tft->fillRect(x + 15, y + 12, 30, 36, COLOR_WHITE);
  tft->fillRect(x + 21, y + 21, 18, 3, 0x7BEF);
  tft->fillRect(x + 21, y + 28, 18, 3, 0x7BEF);
  tft->fillRect(x + 21, y + 36, 12, 3, 0x7BEF);
}

void drawSettingsIcon(int x, int y) {
  tft->fillRoundRect(x, y, 60, 60, COLOR_GREY);
  tft->fillRect(x + 27, y + 12, 6, 36, COLOR_WHITE);
  tft->fillRect(x + 12, y + 27, 36, 6, COLOR_WHITE);
  tft->fillCircle(x + 30, y + 30, 12, COLOR_WHITE);
  tft->fillCircle(x + 30, y + 30, 6, COLOR_GREY);
}

void drawTerminalIcon(int x, int y) {
  tft->fillRoundRect(x, y, 60, 60, COLOR_BLACK);
  tft->drawRoundRect(x, y, 60, 60, COLOR_WHITE);
  tft->fillTriangle(x + 12, y + 18, x + 21, y + 24, x + 12, y + 30, COLOR_GREEN);
  tft->fillRect(x + 24, y + 28, 12, 3, COLOR_GREEN);
}

void drawGoogleIcon(int x, int y) {
  tft->fillRoundRect(x, y, 60, 60, COLOR_WHITE);
  tft->fillCircle(x + 30, y + 30, 16, COLOR_RED);
  tft->fillCircle(x + 30, y + 30, 10, COLOR_WHITE);
  tft->fillRect(x + 30, y + 24, 16, 6, COLOR_BLUE);
  tft->fillRect(x + 30, y + 24, 6, 16, COLOR_BLUE);
}

void drawCalcIcon(int x, int y) {
  tft->fillRoundRect(x, y, 60, 60, COLOR_BLUE);
  tft->fillRect(x + 15, y + 21, 9, 3, COLOR_WHITE);
  tft->fillRect(x + 36, y + 21, 9, 3, COLOR_WHITE);
  tft->fillRect(x + 39, y + 18, 3, 9, COLOR_WHITE);
  tft->fillRect(x + 15, y + 36, 9, 3, COLOR_WHITE);
  tft->fillRect(x + 15, y + 42, 9, 3, COLOR_WHITE);
}

void drawPaintIcon(int x, int y) {
  tft->fillRoundRect(x, y, 60, 60, COLOR_WHITE);
  tft->fillCircle(x + 30, y + 22, 15, COLOR_ORANGE);
  tft->fillCircle(x + 22, y + 28, 6, COLOR_RED);
  tft->fillCircle(x + 38, y + 28, 6, COLOR_BLUE);
  tft->fillCircle(x + 30, y + 36, 6, COLOR_GREEN);
  tft->fillRect(x + 27, y + 40, 6, 14, COLOR_DARKGREY);
}

void drawPhotosIcon(int x, int y) {
  tft->fillRoundRect(x, y, 60, 60, COLOR_BLUE);
  tft->fillRoundRect(x + 9, y + 12, 42, 32, COLOR_WHITE);
  tft->fillTriangle(x + 12, y + 41, x + 25, y + 24, x + 36, y + 41, COLOR_GREEN);
  tft->fillTriangle(x + 27, y + 41, x + 37, y + 27, x + 48, y + 41, 0x4208);
  tft->fillCircle(x + 40, y + 18, 4, COLOR_YELLOW);
}

struct AppItem {
  int id;
  String name;
};

// List your apps in order
AppItem homeApps[] = {
  { 1, "Games" },
  { 2, "Files" },
  { 3, "Notes" },
  { 4, "Settings" },
  { 5, "Terminal" },
  { 6, "Google" },
  { 7, "Calc" },
  { 8, "Paint" },
  { 9, "Photos" }
};
const int HOME_APP_COUNT = 9;


void showHomeScreen() {

  // If your structural subapp flags are active, let them handle the screen redraw
  if (activeSubApp == APP_NOTES) {
    showNotesScreen();
    return;
  } else if (activeSubApp == APP_GOOGLE) {
    initGoogleScreen();  // Launches your original Wikipedia query card engine
    return;
  } else if (activeSubApp == APP_TERMINAL) {
    showTerminalScreen();  // Launches your dark green console environment
    return;
  }

  // Draw your large shape-based icon launcher menu
  setHardwareScrollRow(0);
  tft->fillScreen(COLOR_BLACK);
  tft->draw16bitRGBBitmap(0, 0, (uint16_t *)backgroundImagePixels, 320, 480);
  drawTopStatusBar();

  int startX = 15;
  int startY = 45;
  int spacingX = 75;
  int spacingY = 85;
  int cols = 4;

  for (int i = 0; i < HOME_APP_COUNT; i++) {
    int col = i % cols;
    int row = i / cols;

    int iconX = startX + (col * spacingX);
    int iconY = startY + (row * spacingY);

    switch (homeApps[i].id) {
      case 1: drawGamesIcon(iconX, iconY); break;
      case 2: drawFilesIcon(iconX, iconY); break;
      case 3: drawNotesIcon(iconX, iconY); break;
      case 4: drawSettingsIcon(iconX, iconY); break;
      case 5: drawTerminalIcon(iconX, iconY); break;
      case 6: drawGoogleIcon(iconX, iconY); break;
      case 7: drawCalcIcon(iconX, iconY); break;
      case 8: drawPaintIcon(iconX, iconY); break;
      case 9: drawPhotosIcon(iconX, iconY); break;
    }

    tft->setTextColor(COLOR_WHITE);
    tft->setTextSize(1);
    int textX = iconX + (30 - (homeApps[i].name.length() * 3));
    tft->setCursor(textX, iconY + 65);
    tft->print(homeApps[i].name);
  }
}




void showFilesScreen() {


  // 1. Draw Mock-up Top Status Bar Placeholder
  tft->fillRoundRect(0, 0, 320, 24, COLOR_GREY);
  tft->setTextColor(COLOR_BLACK);
  tft->setTextSize(1);
  tft->setCursor(5, 8);
  tft->print("HOME");
  tft->setCursor(145, 8);
  tft->print("12:34");

  // 2. Draw BACK Button (Y = 30 to 75)
  tft->fillRoundRect(10, 30, 285, 45, 0xFBE4);  // Soft orange color
  tft->setTextColor(COLOR_BLACK);
  tft->setTextSize(3);
  tft->setCursor(120, 42);
  tft->print("BACK");

  // 3. Read fresh data from storage card
  readSDCardFiles();

  // 4. Draw File List Rows (Y starts at 85, each item takes 45px height)
  int currentY = 85;
  for (int i = 0; i < totalFiles; i++) {
    if (currentY > 440) break;  // Screen boundaries safety check

    uint16_t bgColor = COLOR_GREY;
    uint16_t textColor = COLOR_BLACK;
    String typeLabel = "Text Document";

    if (fileList[i].isDirectory) {
      bgColor = 0xFBE4;  // Orange style
      typeLabel = "File Folder";
    } else if (fileList[i].name.endsWith(".ini")) {
      bgColor = COLOR_RED;
      textColor = COLOR_BLACK;
      typeLabel = "System config file";
    } else if (fileList[i].name.endsWith(".uci")) {
      bgColor = COLOR_BLACK;
      textColor = COLOR_WHITE;
      typeLabel = "Uncompressed Image";
    } else if (isImageFileName(fileList[i].name)) {
      bgColor = COLOR_PURPLE;
      textColor = COLOR_WHITE;
      typeLabel = "Photo";
    } else if (fileList[i].name.endsWith(".PNT") || fileList[i].name.endsWith(".pnt")) {
      bgColor = COLOR_GREEN;
      textColor = COLOR_BLACK;
      typeLabel = "Saved Drawing";
    }

    // Draw row background container card
    if (bgColor == COLOR_BLACK) {
      // Draw white outline instead of solid background fill
      tft->drawRoundRect(10, currentY, 285, 40, COLOR_WHITE);
    } else {
      tft->fillRoundRect(10, currentY, 285, 40, bgColor);
    }

    // Draw Primary Filename text string left-aligned
    tft->setTextColor(textColor);
    tft->setTextSize(2);
    tft->setCursor(15, currentY + 12);
    tft->print(fileList[i].name);

    // Draw Subtype description metadata label right-aligned
    tft->setTextSize(1);
    tft->setCursor(185, currentY + 16);
    tft->print(typeLabel);

    currentY += 45;  // Row layout math skip interval
  }

  // 5. Draw Right Scrollbar Graphical Trim Asset
  tft->fillRoundRect(305, 30, 10, 440, 0xFBE4);
}


void readSDCardFiles() {
  totalFiles = 0;
  File root = SD.open("/");
  if (!root) return;

  // Step 1: Gather Folders First
  File entry = root.openNextFile();
  while (entry && totalFiles < 20) {
    if (entry.isDirectory()) {
      fileList[totalFiles] = { String(entry.name()), true };
      totalFiles++;
    }
    entry.close();
    entry = root.openNextFile();
  }

  // Step 2: Gather Files Second
  root.rewindDirectory();
  entry = root.openNextFile();
  while (entry && totalFiles < 20) {
    if (!entry.isDirectory()) {
      // Prevent duplicates by checking if name already exists
      bool exists = false;
      for (int i = 0; i < totalFiles; i++) {
        if (fileList[i].name == entry.name()) exists = true;
      }
      if (!exists) {
        fileList[totalFiles] = { String(entry.name()), false };
        totalFiles++;
      }
    }
    entry.close();
    entry = root.openNextFile();
  }
  root.close();
}

// Near the top of your file (outside of any functions):



void handleHomeTouch(int x, int y) {
  // Keep your top-right corner quick-lock bar shortcut active
  if (x >= 240 && x <= 320 && y >= 0 && y <= 30) {
    currentScreen = STATE_LOCKED;
    showLockScreen();
    delay(300);
    return;
  }

  int startX = 15;
  int startY = 45;
  int spacingX = 75;
  int spacingY = 85;
  int cols = 4;

  for (int i = 0; i < HOME_APP_COUNT; i++) {
    int col = i % cols;
    int row = i / cols;

    int iconX = startX + (col * spacingX);
    int iconY = startY + (row * spacingY);

    if (x >= iconX && x <= (iconX + 60) && y >= iconY && y <= (iconY + 60)) {
      switch (homeApps[i].id) {
        case 1:                                                // Games
          playModernOpenAnimation(iconX, iconY, COLOR_BLACK);  // Calc app background is black
          currentScreen = STATE_GAMES;                         // Shifts to your sub-arcade list
          showGamesScreen();
          break;
        case 2:                                                // Files
          playModernOpenAnimation(iconX, iconY, COLOR_BLACK);  // Calc app background is black
          currentScreen = STATE_FILES;                         // Shifts to your SD library view card
          showFilesScreen();
          break;
        case 3:                                                // Notes
          playModernOpenAnimation(iconX, iconY, COLOR_BLACK);  // Calc app background is black
          notesText = "";                                      // Clear working string buffer like original line
          activeSubApp = APP_NOTES;                            // Activate your QWERTY context layout filter
          showNotesScreen();
          break;
        case 4:                                                // Settings
          playModernOpenAnimation(iconX, iconY, COLOR_BLACK);  // Calc app background is black
          currentScreen = STATE_SETTINGS;
          showSettingsScreen();
          break;
        case 5:                                                // Terminal
          playModernOpenAnimation(iconX, iconY, COLOR_BLACK);  // Calc app background is black
          terminalInput = "";                                  // Clear input prompt buffer safely
          activeSubApp = APP_TERMINAL;
          showTerminalScreen();
          break;
        case 6:                                                // Google
          playModernOpenAnimation(iconX, iconY, COLOR_BLACK);  // Calc app background is black
          googleQuery = "";
          googleResult = "";
          initGoogleScreen();
          break;
        case 7:                                                // Calculator App
          playModernOpenAnimation(iconX, iconY, COLOR_BLACK);  // Calc app background is black
          currentScreen = STATE_CALC;
          calcInput = "";
          showCalculatorScreen();
          break;
        case 8:                                                // Paint App
          playModernOpenAnimation(iconX, iconY, COLOR_WHITE);
          currentScreen = STATE_DRAW;
          clearDrawingCanvas();  // Fresh blank canvas when launched from Home
          showDrawScreen();
          break;
        case 9:                                                // Photos App
          playModernOpenAnimation(iconX, iconY, COLOR_BLACK);
          currentScreen = STATE_PHOTOS;
          photosOpenedFromFiles = false;
          viewingFullscreenImage = false;
          showPhotosGallery();
          break;
      }
      delay(300);
      return;
    }
  }
}





void showTime() {
  drawTopStatusBar();

  struct tm timeinfo;
  String lockTimeStr = "12:00";
  String lockDateStr = "Monday, Aug 4";

  if (getLocalTime(&timeinfo)) {
    char hMin[6];
    strftime(hMin, sizeof(hMin), "%H:%M", &timeinfo);
    char dStr[24];
    strftime(dStr, sizeof(dStr), "%A, %b %d", &timeinfo);
    lockTimeStr = String(hMin);
    lockDateStr = String(dStr);
  }

  tft->setTextColor(COLOR_WHITE);
  tft->setTextSize(4);
  tft->setCursor(40, 60);
  tft->print(lockTimeStr);
  tft->setTextSize(2);
  tft->setCursor(40, 105);
  tft->print(lockDateStr);
}


void showLockScreen() {
  setHardwareScrollRow(0);
  if (currentLockStyle == LOCK_NONE) {
    currentScreen = STATE_HOME;
    showHomeScreen();
    return;
  }

  tft->draw16bitRGBBitmap(0, 0, (uint16_t *)backgroundImagePixels, 320, 480);
  int scroll = 0;
  int maxScroll = 480;

  while (scroll < maxScroll) {
    scroll++;
    setHardwareScrollRow(scroll);

    int distanceLeft = maxScroll - scroll;

    // 1ms baseline delay for a fast start.
    // The curve only kicks in heavily when distanceLeft is under 30 pixels.
    int dynamicDelay = 0 + (300 / (distanceLeft + 5));

    delay(dynamicDelay);
  }

  switch (currentLockStyle) {
    case LOCK_TAP:
      tft->fillRoundRect(60, 340, 200, 50, COLOR_BLUE);
      tft->drawRoundRect(60, 340, 200, 50, COLOR_WHITE);
      tft->setTextSize(2);
      tft->setCursor(95, 355);
      tft->print("TAP TO UNLOCK");
      break;
    case LOCK_SLIDE_UP:

      // Centered at X=160, hovering 15 pixels above the bottom edge
      // Parameters: x, y, width, height, radius, color
      tft->fillRoundRect(60, 455, 200, 10, COLOR_WHITE);





      break;
    case LOCK_SLIDE_RIGHT:
      tft->fillRoundRect(20, 340, 280, 50, COLOR_DARKGREY);
      tft->drawRoundRect(20, 340, 280, 50, COLOR_WHITE);
      tft->fillRoundRect(25, 345, 60, 40, COLOR_BLUE);
      tft->fillTriangle(75, 365, 60, 353, 60, 377, COLOR_WHITE);
      tft->setTextSize(1);
      tft->setCursor(105, 360);
      tft->print("SLIDE RIGHT TO OPEN");
      break;
    case LOCK_PIN:
      tft->fillRoundRect(90, 140, 150, 35, COLOR_DARKGREY);
      tft->drawRoundRect(90, 140, 150, 35, COLOR_WHITE);
      updatePinDots();
      int padX = 40;
      int padY = 200;
      int btnW = 70;
      int btnH = 50;
      int gap = 15;

      // FIXED FIXED SYNTAX CRITICAL ERROR: Restored explicit array dimension bracket keys ([][])
      char keys[4][3] = { { '1', '2', '3' }, { '4', '5', '6' }, { '7', '8', '9' }, { 'C', '0', 'X' } };

      tft->setTextSize(2);
      for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 3; c++) {
          int bx = padX + c * (btnW + gap);
          int by = padY + r * (btnH + gap);
          uint16_t btnColor = COLOR_GREY;
          if (keys[r][c] == 'C') btnColor = COLOR_RED;
          if (keys[r][c] == 'X') btnColor = COLOR_ORANGE;
          tft->fillRoundRect(bx, by, btnW, btnH, btnColor);
          tft->drawRoundRect(bx, by, btnW, btnH, COLOR_WHITE);
          tft->setTextColor(COLOR_WHITE);
          tft->setCursor(bx + 28, by + 18);
          tft->print(keys[r][c]);
        }
      }
      break;
  }
}


// 1. MASTER WI-FI BOOTSTRAPPING ENGINE
void connectToInternetRails() {
  Serial.print("[NET] Initializing Wi-Fi radio stack...");

  // Force station mode explicitly to ensure standard router compatibility
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  tft->fillRoundRect(20, 240, 280, 40, COLOR_BLACK);
  tft->setCursor(40, 240);
  tft->setTextColor(COLOR_ORANGE);
  tft->setTextSize(2);
  tft->print("Connecting Web...");

  int counter = 0;
  // Poll the hardware connection registers over a 15-second safety timeout window
  while (WiFi.status() != WL_CONNECTED && counter < 30) {
    delay(500);
    Serial.print(".");
    counter++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[NET] Connected! IP Address Assigned.");
    tft->fillRoundRect(20, 240, 280, 40, COLOR_BLACK);
    tft->setCursor(40, 240);
    tft->setTextColor(COLOR_GREEN);
    tft->print("WiFi Connected!");

    // Trigger the NTP clock sync hook instantly once the router line opens!
    synchronizeInternalClock();
  } else {
    Serial.println("\n[NET] Wi-Fi connection timed out. Check 2.4GHz bands.");
    tft->fillRoundRect(20, 240, 280, 40, COLOR_BLACK);
    tft->setCursor(40, 240);
    tft->setTextColor(COLOR_RED);
    tft->print("WiFi Timeout!");
  }
}

void setHardwareScrollRow(uint16_t row) {
  bus->beginWrite();
  bus->writeCommand(0x37);
  bus->write(row >> 8);
  bus->write(row & 0xFF);
  bus->endWrite();
}
void updateCalcDisplay() {
  // 1. Instantly wipe the display box canvas area
  tft->fillRoundRect(10, 35, 300, 50, COLOR_DARKGREY);
  tft->setTextColor(COLOR_WHITE);
  tft->setTextSize(3);
  tft->setCursor(20, 48);

  if (calcInput == "" || calcInput == "-") {
    tft->print("0");
  } else {
    tft->print(calcInput);
  }
}

void showCalculatorScreen() {

  drawTopStatusBar();  // Render your persistent system top bar
  updateCalcDisplay();

  tft->setTextSize(3);

  // --- ROW 1: System Actions & Division (Y: 95 to 135) ---
  tft->fillRoundRect(10, 95, 65, 40, COLOR_RED);  // CLR Button
  tft->setTextColor(COLOR_WHITE);
  tft->setCursor(22, 105);
  tft->print("C");

  tft->fillRoundRect(85, 95, 65, 40, COLOR_LIGHTGREY);  // Plus/Minus Toggle
  tft->setTextColor(COLOR_BLACK);
  tft->setCursor(95, 105);
  tft->print("+/-");

  tft->fillRoundRect(160, 95, 65, 40, COLOR_LIGHTGREY);  // Backspace / Delete
  tft->setTextColor(COLOR_BLACK);
  tft->setCursor(180, 105);
  tft->print("<");

  tft->fillRoundRect(235, 95, 75, 40, COLOR_ORANGE);  // Divide
  tft->setTextColor(COLOR_WHITE);
  tft->setCursor(265, 105);
  tft->print("/");

  // --- ROW 2: 7, 8, 9, Multiply (Y: 145 to 185) ---
  tft->fillRoundRect(10, 145, 65, 40, COLOR_LIGHTGREY);
  tft->setTextColor(COLOR_BLACK);
  tft->setCursor(32, 155);
  tft->print("7");
  tft->fillRoundRect(85, 145, 65, 40, COLOR_LIGHTGREY);
  tft->setTextColor(COLOR_BLACK);
  tft->setCursor(107, 155);
  tft->print("8");
  tft->fillRoundRect(160, 145, 65, 40, COLOR_LIGHTGREY);
  tft->setTextColor(COLOR_BLACK);
  tft->setCursor(182, 155);
  tft->print("9");
  tft->fillRoundRect(235, 145, 75, 40, COLOR_ORANGE);
  tft->setTextColor(COLOR_WHITE);
  tft->setCursor(265, 155);
  tft->print("*");

  // --- ROW 3: 4, 5, 6, Subtract (Y: 195 to 235) ---
  tft->fillRoundRect(10, 195, 65, 40, COLOR_LIGHTGREY);
  tft->setTextColor(COLOR_BLACK);
  tft->setCursor(32, 205);
  tft->print("4");
  tft->fillRoundRect(85, 195, 65, 40, COLOR_LIGHTGREY);
  tft->setTextColor(COLOR_BLACK);
  tft->setCursor(107, 205);
  tft->print("5");
  tft->fillRoundRect(160, 195, 65, 40, COLOR_LIGHTGREY);
  tft->setTextColor(COLOR_BLACK);
  tft->setCursor(182, 205);
  tft->print("6");
  tft->fillRoundRect(235, 195, 75, 40, COLOR_ORANGE);
  tft->setTextColor(COLOR_WHITE);
  tft->setCursor(265, 205);
  tft->print("-");

  // --- ROW 4: 1, 2, 3, Add (Y: 245 to 285) ---
  tft->fillRoundRect(10, 245, 65, 40, COLOR_LIGHTGREY);
  tft->setTextColor(COLOR_BLACK);
  tft->setCursor(32, 255);
  tft->print("1");
  tft->fillRoundRect(85, 245, 65, 40, COLOR_LIGHTGREY);
  tft->setTextColor(COLOR_BLACK);
  tft->setCursor(107, 255);
  tft->print("2");
  tft->fillRoundRect(160, 245, 65, 40, COLOR_LIGHTGREY);
  tft->setTextColor(COLOR_BLACK);
  tft->setCursor(182, 255);
  tft->print("3");
  tft->fillRoundRect(235, 245, 75, 40, COLOR_ORANGE);
  tft->setTextColor(COLOR_WHITE);
  tft->setCursor(265, 255);
  tft->print("+");

  // --- ROW 5: 0, Decimal, Equals (Y: 295 to 335) ---
  tft->fillRoundRect(10, 295, 140, 40, COLOR_LIGHTGREY);
  tft->setTextColor(COLOR_BLACK);
  tft->setCursor(70, 305);
  tft->print("0");
  tft->fillRoundRect(160, 295, 65, 40, COLOR_LIGHTGREY);
  tft->setTextColor(COLOR_BLACK);
  tft->setCursor(187, 305);
  tft->print(".");
  tft->fillRoundRect(235, 295, 75, 40, COLOR_GREEN);
  tft->setTextColor(COLOR_BLACK);
  tft->setCursor(265, 305);
  tft->print("=");
}

void updateSettingsToggles() {
  tft->setTextSize(2);
  tft->setTextColor(COLOR_WHITE, COLOR_BLACK);
  tft->fillRoundRect(200, 150, 100, 40, wifiEnabled ? COLOR_GREEN : COLOR_DARKGREY);
  tft->setCursor(235, 162);
  tft->print(wifiEnabled ? "ON " : "OFF");
  tft->fillRoundRect(200, 230, 100, 40, bluetoothEnabled ? COLOR_GREEN : COLOR_DARKGREY);
  tft->setCursor(235, 242);
  tft->print(bluetoothEnabled ? "ON " : "OFF");
}
// Add tracking variables for our settings architecture sub-layers
enum SettingsSubScreen {
  SUB_NONE,
  SUB_SECURITY,
  SUB_WIFI,
  SUB_ACCESSIBILITY,
  SUB_ABOUT,
  SUB_DEV,
  SUB_WIFI_PASS_KEYBOARD

};
SettingsSubScreen currentSubScreen = SUB_NONE;
void renderSettingsRow(int index, String label, uint16_t customBg = COLOR_GREY) {
  int rowStartY = 30;
  int rowHeight = 45;
  uint16_t bgColor = (index == 0) ? COLOR_RED : customBg;

  int rX = 10;
  int rY = rowStartY + (index * rowHeight);
  int rW = 300;
  int rH = 40;

  // FIXED: Safe menu row block draw
  tft->fillRect(rX, rY, rW, rH, bgColor);

  tft->drawRoundRect(rX, rY, rW, rH, COLOR_WHITE);
  tft->setTextColor(COLOR_BLACK);
  tft->setTextSize(2);
  tft->setCursor(20, rY + 12);
  tft->print(label);
}






void showSettingsScreen() {
  tft->fillScreen(COLOR_BLACK);

  if (currentSubScreen == SUB_NONE) {
    String opts[] = { "< BACK", "Wi-Fi Networks", "Developer Tools", "Accessibility" };
    for (int i = 0; i < 4; i++) {
      renderSettingsRow(i, opts[i]);
    }
  } else if (currentSubScreen == SUB_WIFI) {
    // Row 0 is always the back utility navigation button
    renderSettingsRow(0, "< BACK to Settings");

    if (networkCount == 0) {
      renderSettingsRow(1, "Scan for Networks...");
      for (int i = 2; i < 4; i++) renderSettingsRow(i, "---");
    } else {
      for (int i = 1; i < 4; i++) {
        if ((i - 1) < networkCount) {
          renderSettingsRow(i, discoveredSSIDs[i - 1]);
        } else {
          renderSettingsRow(i, "[Empty Slot]");
        }
      }
    }
  } else if (currentSubScreen == SUB_DEV) {
    String opts[] = { "< BACK", "CPU: " + String(ESP.getCpuFreqMHz()) + " MHz", "Factory Reset", "Reboot Board" };
    for (int i = 0; i < 4; i++) {
      renderSettingsRow(i, opts[i]);
    }
  } else if (currentSubScreen == SUB_ACCESSIBILITY) {
    String opts[] = { "< BACK", "High Contrast: ON", "Screen Reader: OFF" };
    for (int i = 0; i < 3; i++) {
      renderSettingsRow(i, opts[i]);
    }
  } else if (currentSubScreen == SUB_WIFI_PASS_KEYBOARD) {
    // 1. Clear top notification background area
    tft->fillRect(0, 0, 320, 320, COLOR_BLACK);

    // 2. Render Text Entry Box Header Info
    tft->setCursor(15, 30);
    tft->setTextColor(COLOR_WHITE);
    tft->setTextSize(2);
    tft->print("Connect to: " + selectedSSID);

    tft->setCursor(15, 70);
    tft->setTextColor(COLOR_GREEN);
    tft->print("Password: " + typingPassword + "_");

    // 3. Draw Gray Keyboard Background Plate at the bottom
    tft->fillRect(0, 320, 320, 160, COLOR_DARKGREY);

    // 4. Define text matrices for Row layouts
    // Define text matrices for Row layouts
    String rows[3];
    if (currentKeyboardPage == 0) {
      rows[0] = "qwertyuiop";
      rows[1] = "asdfghjkl";
      rows[2] = "zxcvbnm";

      // FIXED: Automatically convert the text maps to uppercase visually if shift is toggled
      if (isUppercase) {
        rows[0].toUpperCase();
        rows[1].toUpperCase();
        rows[2].toUpperCase();
      }
    } else {
      rows[0] = "1234567890";
      rows[1] = "-/:;()$&@\"";
      rows[2] = ".,?!'";
    }


    tft->setTextSize(2);
    tft->setTextColor(COLOR_WHITE);

    tft->setTextSize(2);
    tft->setTextColor(COLOR_WHITE);

    // Row 1 - 10 keys
    for (int k = 0; k < rows[0].length(); k++) {
      int x_pos = k * 32;
      tft->drawRect(x_pos + 1, 320 + 2, 30, 36, COLOR_BLACK);
      tft->setCursor(x_pos + 10, 320 + 12);
      tft->print(rows[0][k]);  // Will visually render capital/lowercase letters dynamically
    }

    // Row 2 - Offset slightly inward
    for (int k = 0; k < rows[1].length(); k++) {
      int x_pos = 16 + (k * 32);
      tft->drawRect(x_pos + 1, 360 + 2, 30, 36, COLOR_BLACK);
      tft->setCursor(x_pos + 10, 360 + 12);
      tft->print(rows[1][k]);
    }

    // Row 3 - Shift, Letters, Backspace
    // [SHIFT BUTTON COLOR STATE CHANGER]
    if (currentKeyboardPage == 0 && isUppercase) {
      tft->fillRect(2, 400 + 2, 40, 36, COLOR_GREEN);  // Active caps indication tint
      tft->setTextColor(COLOR_BLACK);
    } else {
      tft->fillRect(2, 400 + 2, 40, 36, COLOR_LIGHTGREY);
      tft->setTextColor(COLOR_WHITE);
    }
    tft->setCursor(10, 400 + 12);
    tft->print("^");
    tft->setTextColor(COLOR_WHITE);  // Safe reset loop anchor

    // Render Row 3 Characters
    for (int k = 0; k < rows[2].length(); k++) {
      int x_pos = 45 + (k * 32);
      tft->drawRect(x_pos + 1, 400 + 2, 30, 36, COLOR_BLACK);
      tft->setCursor(x_pos + 10, 400 + 12);
      tft->print(rows[2][k]);
    }

    // [BACKSPACE BUTTON]
    tft->fillRect(273, 400 + 2, 45, 36, COLOR_LIGHTGREY);
    tft->setCursor(280, 400 + 12);
    tft->print("<-");

    // Row 4 (Utility Command row)
    // [?123 Mode Switch Button]
    tft->fillRect(2, 440 + 2, 60, 36, COLOR_LIGHTGREY);
    tft->setCursor(10, 440 + 12);
    tft->print("?123");

    // [SPACE BAR]
    tft->fillRect(66, 440 + 2, 160, 36, COLOR_BLACK);

    // [CONNECT BUTTON] (Represented by the blue search icon color from your image)
    tft->fillRect(230, 440 + 2, 88, 36, COLOR_BLUE);
    tft->setCursor(240, 440 + 12);
    tft->setTextColor(COLOR_WHITE);
    tft->print("CONN");
  }
}




void handleSettingsTouch(int x, int y) {
  // 1. Calculate which matrix row index was targeted by the touch
  // Adjust 40 (starting Y margin) and 50 (row layout height) if your layout changes!
  int i = (y - 40) / 50;
  if (i < 0) i = 0;
  if (i > 4) i = 4;  // Caps at 4 to cover the extended keyboard bottom actions row

  // ==========================================
  // MAIN SETTINGS NAVIGATION
  // ==========================================
  if (currentSubScreen == SUB_NONE) {
    if (i == 0) {
      currentScreen = STATE_HOME;
      showHomeScreen();
    } else if (i == 1) {
      currentSubScreen = SUB_WIFI;
      showSettingsScreen();
    } else if (i == 2) {
      currentSubScreen = SUB_DEV;
      showSettingsScreen();
    } else if (i == 3) {
      currentSubScreen = SUB_ACCESSIBILITY;
      showSettingsScreen();
    }
  }
  // ==========================================
  // WI-FI DISCOVERY MENU SUB-SCREEN
  // ==========================================
  else if (currentSubScreen == SUB_WIFI) {
    if (i == 0) {
      currentSubScreen = SUB_NONE;
      showSettingsScreen();
    } else if (networkCount == 0 && i == 1) {
      tft->fillScreen(COLOR_BLACK);
      tft->setCursor(40, 200);
      tft->setTextColor(COLOR_WHITE);
      tft->print("Scanning Airwaves...");
      scanForNetworks();
      showSettingsScreen();
    } else if (networkCount > 0 && (i - 1) < networkCount) {
      // Logic moved here so variable 'i' resolves properly in scope
      selectedSSID = discoveredSSIDs[i - 1];
      typingPassword = "";
      currentSubScreen = SUB_WIFI_PASS_KEYBOARD;
      showSettingsScreen();
    }
  }
  // ==========================================
  // REBOOT / L&D DIAGNOSTICS (DEVELOPER)
  // ==========================================
  else if (currentSubScreen == SUB_DEV) {
    if (i == 0) {
      currentSubScreen = SUB_NONE;
      showSettingsScreen();
    } else if (i == 2) {
      tft->fillScreen(COLOR_BLACK);
      tft->setCursor(40, 200);
      tft->setTextColor(COLOR_RED);
      tft->setTextSize(2);
      tft->print("Wiping Flash...");

      prefs.begin("phone_sys", false);
      prefs.clear();
      prefs.end();

      delay(2000);
      ESP.restart();
    } else if (i == 3) {
      tft->fillScreen(COLOR_BLACK);
      tft->setCursor(100, 120);
      tft->print("Rebooting...");
      delay(1000);
      ESP.restart();
    }
  }
  // ==========================================
  // ACCESSIBILITY SUB-SCREEN
  // ==========================================
  else if (currentSubScreen == SUB_ACCESSIBILITY) {
    if (i == 0) {
      currentSubScreen = SUB_NONE;
      showSettingsScreen();
    }
  }
  // ==========================================
  // WI-FI KEYBOARD INTERFACE STATE
  // ==========================================
  // If user touches the top area, ignore typing interactions
  if (y < 320) return;

  // Define text matrices to match display rendering rules
  String rows[3];
  if (currentKeyboardPage == 0) {
    rows[0] = "qwertyuiop";
    rows[1] = "asdfghjkl";
    rows[2] = "zxcvbnm";
  } else {
    rows[0] = "1234567890";
    rows[1] = "-/:;()$&@\"";
    rows[2] = ".,?!'";
  }

  // --- ROW 1 TOUCH HANDLING ---
  if (y >= 320 && y < 360) {
    int idx = x / 32;
    if (idx >= 0 && idx < rows[0].length()) {
      char pressedChar = rows[0][idx];
      if (isUppercase) pressedChar = toupper(pressedChar);
      typingPassword += pressedChar;
    }
  }
  // --- ROW 2 TOUCH HANDLING ---
  else if (y >= 360 && y < 400) {
    int idx = (x - 16) / 32;
    if (idx >= 0 && idx < rows[1].length()) {
      char pressedChar = rows[1][idx];
      if (isUppercase) pressedChar = toupper(pressedChar);
      typingPassword += pressedChar;
    }
  }

  // --- ROW 3 TOUCH HANDLING ---
  else if (y >= 400 && y < 440) {
    if (x < 45) {
      // FIXED: Toggle character casing state
      isUppercase = !isUppercase;
    } else if (x >= 273) {  // Backspace action
      if (typingPassword.length() > 0) {
        typingPassword.remove(typingPassword.length() - 1);
      }
    } else {
      int idx = (x - 45) / 32;
      if (idx >= 0 && idx < rows[2].length()) {
        char pressedChar = rows[2][idx];
        // Apply uppercase logic if toggled
        if (isUppercase) {
          pressedChar = toupper(pressedChar);
        }
        typingPassword += pressedChar;
      }
    }
  }

  // --- ROW 4 UTILITY ROW TOUCH HANDLING ---
  else if (y >= 440 && y < 480) {
    if (x < 66) {
      // Toggle between symbols/numbers and letters
      currentKeyboardPage = !currentKeyboardPage;
    } else if (x >= 66 && x < 226) {
      // Insert Space
      typingPassword += " ";
    } else if (x >= 226) {
      // CONNECT INITIATION CODE
      tft->fillScreen(COLOR_BLACK);
      tft->setCursor(20, 200);
      tft->print("Connecting to network...");

      WiFi.disconnect();
      WiFi.begin(selectedSSID.c_str(), typingPassword.c_str());

      int attempts = 0;
      while (WiFi.status() != WL_CONNECTED && attempts < 15) {
        delay(500);
        attempts++;
      }

      if (WiFi.status() == WL_CONNECTED) {
        currentSubScreen = SUB_NONE;
      }
    }
  }

  // Refresh layout view adjustments instantly
  showSettingsScreen();
}






bool checkLockdownActive() {
  if (millis() < lockUntilTime) {
    unsigned long remainingMillis = lockUntilTime - millis();
    int remainingSeconds = (remainingMillis / 1000) + 1;
    tft->fillScreen(COLOR_BLACK);
    tft->setTextColor(COLOR_RED);
    tft->setTextSize(3);
    tft->setCursor(20, 150);
    tft->print("LOCKOUT ACTIVE");
    tft->setTextColor(COLOR_WHITE);
    tft->setTextSize(2);
    tft->setCursor(20, 200);
    tft->print("Try again in: ");
    tft->print(remainingSeconds);
    tft->print(" s");
    delay(1000);
    showLockScreen();
    return true;
  }
  return false;
}

void parseKeyboardInput(int x, int y) {
  if (y < 260) return;  // Ignore taps outside the keyboard bounds
  String pressedChar = "";
  bool isDelete = false;
  bool isAction = false;

  // Row One Translation Math
  if (y >= 270 && y <= 310) {
    int keyIdx = x / 31;
    if (keyIdx >= 0 && keyIdx < 10) {
      String r1[] = { "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P" };
      pressedChar = r1[keyIdx];
    }
  }
  // Row Two Translation Math
  else if (y >= 320 && y <= 360) {
    int keyIdx = x / 31;
    if (keyIdx >= 0 && keyIdx < 10) {
      if (keyIdx == 9) isDelete = true;
      else {
        String r2[] = { "A", "S", "D", "F", "G", "H", "J", "K", "L" };
        pressedChar = r2[keyIdx];
      }
    }
  }
  // Row Three Translation Math
  else if (y >= 370 && y <= 410) {
    int keyIdx = (x - 15) / 31;
    if (keyIdx >= 0 && keyIdx < 9) {
      String r3[] = { "Z", "X", "C", "V", "B", "N", "M", ",", "." };
      pressedChar = r3[keyIdx];
    }
  }
  // Spacebar and Go Action Selectors
  else if (y >= 420 && y <= 460) {
    if (x >= 15 && x <= 215) pressedChar = " ";
    if (x >= 225 && x <= 305) isAction = true;
  }

  // Append or truncate targeted data variables based on active screens
  if (activeSubApp == APP_NOTES) {
    if (isDelete) {
      if (notesText.length() > 0) notesText.remove(notesText.length() - 1);
    } else if (notesText.length() < 60) notesText += pressedChar;
    showNotesScreen();
  } else if (activeSubApp == APP_GOOGLE) {
    if (isDelete) {
      if (googleQuery.length() > 0) googleQuery.remove(googleQuery.length() - 1);
    } else if (isAction) {
      executeGoogleSearch(googleQuery);
    } else if (googleQuery.length() < 24) googleQuery += pressedChar;
    showGoogleScreen();
  }
  // --- ADD THIS BLOCK FOR YOUR TERMINAL APP ---
  else if (activeSubApp == APP_TERMINAL) {
    if (isDelete) {
      if (terminalInput.length() > 0) terminalInput.remove(terminalInput.length() - 1);
    } else if (isAction) {
      processTerminalCommand(terminalInput);  // Runs the command when SEND/RUN is pressed
    } else if (terminalInput.length() < 30) {
      terminalInput += pressedChar;
    }
    showTerminalScreen();  // Re-render the dark console screen with your fresh characters
  }

  delay(150);  // Direct key debounce delay pad
}

// Append these launch initialization parameters right into your setup() function:


void drawQWERTYKeyboard() {
  tft->fillRoundRect(0, 260, 320, 220, COLOR_DARKGREY);
  tft->setTextSize(2);
  tft->setTextColor(COLOR_WHITE);

  // Row 1 Matrix Mapping Layout
  String r1[] = { "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P" };
  for (int i = 0; i < 10; i++) {
    int bx = 2 + i * 31;
    int by = 270;
    tft->fillRoundRect(bx, by, 28, 40, COLOR_GREY);
    tft->drawRoundRect(bx, by, 28, 40, COLOR_WHITE);
    tft->setCursor(bx + 8, by + 12);
    tft->print(r1[i]);
  }
  // Row 2 Matrix Mapping Layout
  String r2[] = { "A", "S", "D", "F", "G", "H", "J", "K", "L", "X" };  // X acts as Backspace delete
  for (int i = 0; i < 10; i++) {
    int bx = 2 + i * 31;
    int by = 320;
    uint16_t keyCol = (i == 9) ? COLOR_RED : COLOR_GREY;
    tft->fillRoundRect(bx, by, 28, 40, keyCol);
    tft->drawRoundRect(bx, by, 28, 40, COLOR_WHITE);
    tft->setCursor(bx + 8, by + 12);
    tft->print(r2[i]);
  }
  // Row 3 Matrix Mapping Layout & Control Toggles
  String r3[] = { "Z", "X", "C", "V", "B", "N", "M", ",", ".", " " };
  for (int i = 0; i < 9; i++) {
    int bx = 15 + i * 31;
    int by = 370;
    tft->fillRoundRect(bx, by, 28, 40, COLOR_GREY);
    tft->drawRoundRect(bx, by, 28, 40, COLOR_WHITE);
    tft->setCursor(bx + 8, by + 12);
    tft->print(r3[i]);
  }
  // Spacebar and Enter Gateway Triggers
  tft->fillRoundRect(15, 420, 200, 40, COLOR_BLUE);
  tft->drawRoundRect(15, 420, 200, 40, COLOR_WHITE);
  tft->setCursor(95, 432);
  tft->print("SPACE");
  tft->fillRoundRect(225, 420, 80, 40, COLOR_GREEN);
  tft->drawRoundRect(225, 420, 80, 40, COLOR_WHITE);
  tft->setCursor(240, 432);
  tft->print("SEND");
}

void handleFilesTouch(int x, int y) {
  // 1. BACK Button Vector Check (Y = 30 to 75)
  if (x >= 10 && x <= 295 && y >= 30 && y <= 75) {
    currentScreen = STATE_HOME;
    activeSubApp = APP_NONE;  // Reset sub-apps pointer
    showHomeScreen();
    delay(300);
    return;
  }

  // 2. Loop Through and Check Every File Row Card Matrix
  int rowStartY = 85;

  for (int i = 0; i < totalFiles; i++) {
    int rowMinY = rowStartY + (i * 45);
    int rowMaxY = rowMinY + 40;

    if (x >= 10 && x <= 295 && y >= rowMinY && y <= rowMaxY) {

      if (fileList[i].isDirectory) {
        Serial.println("Selected folder: " + fileList[i].name);
      } else if (fileList[i].name.endsWith(".txt") || fileList[i].name.endsWith(".ini")) {
        Serial.println("Opening text asset file: " + fileList[i].name);

        File targetFile = SD.open("/" + fileList[i].name);
        if (targetFile) {
          notesText = "";

          while (targetFile.available() && notesText.length() < 1000) {
            notesText += (char)targetFile.read();
          }
          targetFile.close();

          // LOCK IN THE FILENAME POINTER reference so "SEND" can resave changes
          currentOpenedFileName = fileList[i].name;

          // --- UPDATED APP SUB-STATE DISPATCH ALIGNMENT ---
          currentScreen = STATE_HOME;  // Keeps home core dispatcher active
          activeSubApp = APP_NOTES;    // Correctly sets the subapp context enum flag!

          showNotesScreen();
          delay(300);
        }
      } else if (isImageFileName(fileList[i].name)) {
        Serial.println("Opening image file: " + fileList[i].name);

        photosOpenedFromFiles = true;  // So BACK from the viewer returns here, not the gallery
        currentScreen = STATE_PHOTOS;
        showImageFullscreen(fileList[i].name);
        delay(300);
      } else if (fileList[i].name.endsWith(".PNT") || fileList[i].name.endsWith(".pnt")) {
        Serial.println("Opening saved drawing: " + fileList[i].name);

        loadDrawingFromSD(fileList[i].name);
        currentScreen = STATE_DRAW;
        showDrawScreen();
        delay(300);
      }
      break;
    }
  }
}
void drawNotepadTextRegion() {
  // FIXED: Point exclusively to your notepad canvas instance
  notepadCanvas->fillScreen(COLOR_WHITE);  // Canvas background color
  notepadCanvas->setTextColor(COLOR_BLACK);
  notepadCanvas->setTextSize(2);
  notepadCanvas->setCursor(5, 15);

  if (notesText == "") {
    notepadCanvas->print("Start typing...");
  } else {
    notepadCanvas->print(notesText);
  }

  // FIXED: Native flush automatically pushes to X=10, Y=45 with zero flicker
  notepadCanvas->flush();
}

void drawTerminalInputRegion() {
  // FIXED: Point exclusively to your terminal canvas instance
  terminalCanvas->fillScreen(0x10A2);  // Dark terminal background color
  terminalCanvas->setTextColor(COLOR_GREEN);
  terminalCanvas->setTextSize(2);
  terminalCanvas->setCursor(5, 15);
  terminalCanvas->print("> " + terminalInput + "_");

  // FIXED: Native flush automatically pushes to X=10, Y=230 with zero flicker
  terminalCanvas->flush();
}

void showNotesScreen() {
  drawTopStatusBar();
  activeSubApp = APP_NOTES;

  int winX = 10;
  int winY = 35;
  int winW = 300;
  int winH = 210;

  // This full fill and outline should ONLY run once on app load!
  tft->fillRect(winX, winY, winW, winH, COLOR_WHITE);
  tft->drawRoundRect(winX, winY, winW, winH, COLOR_BLUE);

  // Draw the keyboard once on load
  drawQWERTYKeyboard();

  // FIXED: Call your canvas update function to cleanly render the current text state
  drawNotepadTextRegion();
}

// RUN THIS ONLY ONCE: When clicking the app icon from the home launcher screen
void initGoogleScreen() {
  activeSubApp = APP_GOOGLE;

  // Draw the entire screen infrastructure completely once

  drawTopStatusBar();

  // Static results container box
  tft->fillRoundRect(10, 95, 300, 155, COLOR_DARKGREY);
  tft->setTextColor(COLOR_WHITE);
  tft->setTextSize(1);
  tft->setCursor(15, 105);
  tft->print(googleResult == "" ? "Results view field" : googleResult);

  // Static keyboard framework
  drawQWERTYKeyboard();

  // Call the high-speed input text draw function to fill in the text box
  showGoogleScreen();
}

// RUN THIS EVERY KEYSTROKE: Blazing fast partial update loop
void showGoogleScreen() {
  // Wipe ONLY the small white search box row layer (Extremely fast write cycle!)
  tft->fillRoundRect(10, 35, 300, 50, COLOR_WHITE);

  // Instantly print your active typed string over it
  tft->setTextColor(COLOR_BLACK);
  tft->setTextSize(2);
  tft->setCursor(20, 52);
  tft->print(googleQuery == "" ? "Search Google..." : googleQuery);
}





// REMOVED FADE EFFECT: Wemos module drops the wallpaper and snaps home instantly
void executeInstantUnlock() {
  Serial.println("[SYSTEM ENGINE] Clearing I2C streams for instant change...");
  currentScreen = STATE_HOME;
  Wire.endTransmission(true);
  delay(30);

  showHomeScreen();
  Wire.begin(TOUCH_SDA, TOUCH_SCL);
  delay(50);
  Serial.println("[SYSTEM READY] App controls responsive.");
}

void gotoUnlockGateway() {
  if (pinRequired) {
    LockStyle savedStyle = currentLockStyle;
    currentLockStyle = LOCK_PIN;
    showLockScreen();
    currentLockStyle = savedStyle;
  } else {
    executeInstantUnlock();
  }
  delay(200);
}
// Put this declaration at the very top header level of your script file:
// FIXED: Declared globally so all your lock screen functions can see them!
int lastDeltaY_Global = 0;
const int UNLOCK_DISTANCE = 340;

// Put this tracking flag at the very top header level of your sketch file:
bool isAnimatingLockScreen = false;

// ==========================================================================
// SLIDE DOWN ANIMATION
// Plays whenever the user lets go of the lock screen sheet without dragging
// it past UNLOCK_DISTANCE. Eases the sheet back down from its current
// dragged position (startY) to the resting position (0) using an ease-out
// curve, then redraws a clean flat frame once it settles.
// ==========================================================================
void slideDownLockScreen(int startY) {
  const int animationFrames = 15;

  for (int frame = 0; frame <= animationFrames; frame++) {
    float progressTime = (float)frame / animationFrames;
    // Smooth Ease-Out formula
    float easeCurveFactor = 1.0f - (1.0f - progressTime) * (1.0f - progressTime);
    int dynamicCurrentY = startY - (int)(startY * easeCurveFactor);

    // Adjust the window position on the display hardware
    setHardwareScrollRow(dynamicCurrentY);

    // Mask the empty canvas window area at the bottom cleanly
    if (dynamicCurrentY > 0) {
      tft->fillRect(0, 480 - dynamicCurrentY, 320, 24, COLOR_BLACK);
    }

    delay(10);  // Clear pacing lock
  }

  // FINAL FRAME SNAP: Re-render the complete layout flat once the slide finishes
  setHardwareScrollRow(0);
  tft->draw16bitRGBBitmap(0, 0, (uint16_t *)backgroundImagePixels, 320, 480);

  // Draw the bottom white capsule bar tracking indicator safely back at home base
  tft->fillRoundRect(60, 455, 200, 10, COLOR_WHITE);

  // Redraw font structures only ONCE at the very end to prevent flickering
  showTime();
  drawTopStatusBar();
}

void handleLockTouchRelease() {
  extern int touchStartPos;
  extern bool isSliding;

  // Activate the master animation lock to block loop conflicts
  isAnimatingLockScreen = true;
  isSliding = false;

  // A. IPHONE ACCELERATION UNLOCK
  if (lastDeltaY_Global > UNLOCK_DISTANCE) {
    for (int pos = lastDeltaY_Global; pos <= 480; pos += 24) {
      setHardwareScrollRow(pos);
      tft->fillRect(0, pos - 24, 320, 24, COLOR_BLACK);
      delay(5);
    }
    setHardwareScrollRow(0);
    isAnimatingLockScreen = false;  // Release system lock
    gotoUnlockGateway();
  }
  // B. SLIDE DOWN (release before reaching the unlock threshold)
  else {
    slideDownLockScreen(lastDeltaY_Global);
  }

  touchStartPos = -1;
  lastDeltaY_Global = 0;          // Reset tracker so the next drag starts clean
  isAnimatingLockScreen = false;  // Open system constraints up for regular processing
}




void saveCurrentNoteToSD() {
  if (currentOpenedFileName == "") {
    // If they opened Notes directly without opening a file first, give it a default name
    currentOpenedFileName = "UNTITLED.TXT";
  }

  Serial.println("Saving changes to: " + currentOpenedFileName);

  // Open the file with FILE_WRITE. (This automatically deletes old contents and starts fresh)
  File saveFile = SD.open("/" + currentOpenedFileName, FILE_WRITE);

  if (saveFile) {
    saveFile.print(notesText);  // Write out the updated text string buffer
    saveFile.close();
    Serial.println("-> Save Successful!");

    // Draw a quick success confirmation pop-up alert box on screen
    tft->fillRoundRect(60, 200, 200, 80, COLOR_GREEN);
    tft->setTextColor(COLOR_WHITE);
    tft->setTextSize(2);
    tft->setCursor(110, 230);
    tft->print("SAVED OK");
    delay(800);

    // Refresh the screen layout
    showNotesScreen();
  } else {
    Serial.println("-> Error: Failed to open file for writing!");
  }
}
void handleLockTouch(int x, int y) {
  if (checkLockdownActive()) return;

  // ==========================================
  // ADJUSTABLE TRAVEL DISTANCE THRESHOLD
  // Default is 210 pixels.
  // Change this number to alter your required swipe distance!
  // ==========================================
  const int UNLOCK_DISTANCE = 340;



  if (currentLockStyle == LOCK_SLIDE_UP) {

    // 1. Swipe Initialization Setup
    if (!isSliding && y > 320) {
      touchStartPos = y;
      isSliding = true;
      lastDeltaY_Global = 0;
    }
    // 2. Cohesive Layer Slide Mechanics
    else if (isSliding) {
      int deltaY = touchStartPos - y;

      // Clip constraints strictly to the display boundaries
      if (deltaY < 0) deltaY = 0;
      if (deltaY > 480) deltaY = 480;

      if (abs(deltaY - lastDeltaY_Global) > 1) {

        // Push the rolling hardware window offset to slide the whole screen up
        setHardwareScrollRow(deltaY);

        if (deltaY > lastDeltaY_Global) {
          // Moving UP: The top rows wrap around to the bottom of the screen.
          // We overwrite those physical memory rows with black so a clean black void
          // appears underneath the rising lock screen sheet.
          tft->fillRect(0, lastDeltaY_Global, 320, (deltaY - lastDeltaY_Global), COLOR_BLACK);
        } else if (deltaY < lastDeltaY_Global) {
          // Moving DOWN: As you drag the sheet back down, the black rows move toward the top.
          // We instantly restore the original wallpaper pixels into those physical rows
          // BEFORE they slide back into view, making the downward drag perfectly fluid.
          tft->draw16bitRGBBitmap(0, deltaY, (uint16_t *)(backgroundImagePixels + (deltaY * 320)), 320, (lastDeltaY_Global - deltaY));

          // Re-render text frames inline over the restored wallpaper pixels
          if (deltaY < 140) {
            showTime();
            drawTopStatusBar();
          }
        }

        // FIXED: Track the real drag position in the shared global so
        // handleLockTouchRelease() knows exactly where to slide down from.
        lastDeltaY_Global = deltaY;
      }

      // 3. Standard Phone Unlock Threshold
      if (deltaY > UNLOCK_DISTANCE) {
        isSliding = false;
        touchStartPos = -1;
        lastDeltaY_Global = 0;

        // Reset display register to baseline 0 so the Home screen launcher draws perfectly flat
        setHardwareScrollRow(0);
        tft->fillScreen(COLOR_BLACK);
        gotoUnlockGateway();
      }
    }
  }
}





void handleCalcTouch(int x, int y) {
  // Home / Back Button escape route vector sequence code
  if (x >= 0 && x <= 60 && y >= 0 && y <= 24) {
    currentScreen = STATE_HOME;
    showHomeScreen();
    delay(300);
    return;
  }

  bool changed = false;

  // --- ROW 1 TOUCH CORRIDOR (Y: 95 to 135) ---
  if (y >= 95 && y <= 135) {
    if (x >= 10 && x <= 75) {  // Clear (C)
      calcInput = "";
      firstNum = 0;
      calcOp = ' ';
      clearOnNextKey = false;
      changed = true;
    } else if (x >= 85 && x <= 150) {  // Negate Button (+/-)
      if (calcInput.startsWith("-")) calcInput.remove(0, 1);
      else if (calcInput != "") calcInput = "-" + calcInput;
      changed = true;
    } else if (x >= 160 && x <= 225) {  // Backspace (<)
      if (calcInput.length() > 0) calcInput.remove(calcInput.length() - 1, 1);
      changed = true;
    } else if (x >= 235 && x <= 310) {  // Divide (/)
      if (calcInput != "") {
        firstNum = calcInput.toDouble();
        calcOp = '/';
        calcInput = "";
        clearOnNextKey = false;
      }
    }
  }

  // --- ROW 2 TOUCH CORRIDOR (Y: 145 to 185) ---
  else if (y >= 145 && y <= 185) {
    if (x >= 10 && x <= 75) {
      if (clearOnNextKey) {
        calcInput = "";
        clearOnNextKey = false;
      }
      calcInput += "7";
      changed = true;
    } else if (x >= 85 && x <= 150) {
      if (clearOnNextKey) {
        calcInput = "";
        clearOnNextKey = false;
      }
      calcInput += "8";
      changed = true;
    } else if (x >= 160 && x <= 225) {
      if (clearOnNextKey) {
        calcInput = "";
        clearOnNextKey = false;
      }
      calcInput += "9";
      changed = true;
    } else if (x >= 235 && x <= 310) {
      if (calcInput != "") {
        firstNum = calcInput.toDouble();
        calcOp = '*';
        calcInput = "";
        clearOnNextKey = false;
      }
    }
  }

  // --- ROW 3 TOUCH CORRIDOR (Y: 195 to 235) ---
  else if (y >= 195 && y <= 235) {
    if (x >= 10 && x <= 75) {
      if (clearOnNextKey) {
        calcInput = "";
        clearOnNextKey = false;
      }
      calcInput += "4";
      changed = true;
    } else if (x >= 85 && x <= 150) {
      if (clearOnNextKey) {
        calcInput = "";
        clearOnNextKey = false;
      }
      calcInput += "5";
      changed = true;
    } else if (x >= 160 && x <= 225) {
      if (clearOnNextKey) {
        calcInput = "";
        clearOnNextKey = false;
      }
      calcInput += "6";
      changed = true;
    } else if (x >= 235 && x <= 310) {
      if (calcInput != "") {
        firstNum = calcInput.toDouble();
        calcOp = '-';
        calcInput = "";
        clearOnNextKey = false;
      }
    }
  }

  // --- ROW 4 TOUCH CORRIDOR (Y: 245 to 285) ---
  else if (y >= 245 && y <= 285) {
    if (x >= 10 && x <= 75) {
      if (clearOnNextKey) {
        calcInput = "";
        clearOnNextKey = false;
      }
      calcInput += "1";
      changed = true;
    } else if (x >= 85 && x <= 150) {
      if (clearOnNextKey) {
        calcInput = "";
        clearOnNextKey = false;
      }
      calcInput += "2";
      changed = true;
    } else if (x >= 160 && x <= 225) {
      if (clearOnNextKey) {
        calcInput = "";
        clearOnNextKey = false;
      }
      calcInput += "3";
      changed = true;
    } else if (x >= 235 && x <= 310) {
      if (calcInput != "") {
        firstNum = calcInput.toDouble();
        calcOp = '+';
        calcInput = "";
        clearOnNextKey = false;
      }
    }
  }

  // --- ROW 5 TOUCH CORRIDOR (Y: 295 to 335) ---
  else if (y >= 295 && y <= 335) {
    if (x >= 10 && x <= 150) {  // Double width '0' button area
      if (clearOnNextKey) {
        calcInput = "";
        clearOnNextKey = false;
      }
      if (calcInput != "0") {
        calcInput += "0";
        changed = true;
      }
    } else if (x >= 160 && x <= 225) {  // Decimal point (.)
      if (clearOnNextKey) {
        calcInput = "";
        clearOnNextKey = false;
      }
      if (calcInput.indexOf('.') == -1) {  // Prevents typing double decimal marks
        if (calcInput == "") calcInput = "0";
        calcInput += ".";
        changed = true;
      }
    } else if (x >= 235 && x <= 310) {  // Equals Execution Engine Row (=)
      if (calcInput != "" && calcOp != ' ') {
        double secondNum = calcInput.toDouble();
        double result = 0;
        bool mathError = false;

        if (calcOp == '+') result = firstNum + secondNum;
        else if (calcOp == '-') result = firstNum - secondNum;
        else if (calcOp == '*') result = firstNum * secondNum;
        else if (calcOp == '/') {
          if (secondNum != 0) result = firstNum / secondNum;
          else mathError = true;
        }

        if (mathError) {
          calcInput = "Error";
        } else {
          // Format handling to trim unnecessary floating points (e.g. 5.00 -> 5)
          calcInput = String(result, 4);
          while (calcInput.endsWith("0")) calcInput.remove(calcInput.length() - 1);
          if (calcInput.endsWith(".")) calcInput.remove(calcInput.length() - 1);
        }

        calcOp = ' ';
        clearOnNextKey = true;  // Signals calculation sequence is finalized
        changed = true;
      }
    }
  }

  if (changed) {
    updateCalcDisplay();
    delay(200);  // Standard input switch debouncer protection gap
  }
}




// =========================================================================
// PAINT (DRAWING) APP
// =========================================================================

// Canvas region: full width, between the top bar and the toolbar
const int PAINT_CANVAS_Y0 = 26;
const int PAINT_CANVAS_Y1 = 380;

void drawInterpolatedLine(int x0, int y0, int x1, int y1, uint16_t color, uint8_t size) {
  int dx = x1 - x0;
  int dy = y1 - y0;
  int steps = max(abs(dx), abs(dy));
  if (steps == 0) {
    tft->fillCircle(x1, y1, size / 2, color);
    return;
  }
  for (int i = 0; i <= steps; i++) {
    int ix = x0 + (dx * i) / steps;
    int iy = y0 + (dy * i) / steps;
    tft->fillCircle(ix, iy, size / 2, color);
  }
}

void recordDrawPoint(int x, int y, uint16_t color, uint8_t size, bool newStroke) {
  if (drawPointCount >= MAX_DRAW_POINTS) return;  // Buffer full; drawing stays visible, just stops recording
  drawPoints[drawPointCount].x = x;
  drawPoints[drawPointCount].y = y;
  drawPoints[drawPointCount].color = color;
  drawPoints[drawPointCount].size = size;
  drawPoints[drawPointCount].newStroke = newStroke;
  drawPointCount++;
}

void redrawDrawingFromPoints() {
  int px = -1, py = -1;
  for (int i = 0; i < drawPointCount; i++) {
    DrawPoint &p = drawPoints[i];
    if (p.newStroke || px == -1) {
      tft->fillCircle(p.x, p.y, p.size / 2, p.color);
    } else {
      drawInterpolatedLine(px, py, p.x, p.y, p.color, p.size);
    }
    px = p.x;
    py = p.y;
  }
}

void clearDrawingCanvas() {
  drawPointCount = 0;
  lastDrawX = -1;
  lastDrawY = -1;
  currentDrawingFileName = "";
  tft->fillRect(0, PAINT_CANVAS_Y0, 320, PAINT_CANVAS_Y1 - PAINT_CANVAS_Y0, COLOR_WHITE);
}

void drawPaintToolbar() {
  tft->fillRect(0, 380, 320, 100, COLOR_DARKGREY);

  // Row 1: 8 color swatches
  for (int i = 0; i < 8; i++) {
    int bx = i * 40 + 3;
    tft->fillRoundRect(bx, 385, 34, 34, paintPalette[i]);
    tft->drawRoundRect(bx, 385, 34, 34, COLOR_GREY);

    bool isSelected = eraserActive ? (i == 1) : (!eraserActive && paintPalette[i] == currentDrawColor && i != 1);
    if (isSelected) {
      tft->drawRoundRect(bx - 2, 383, 38, 38, COLOR_GREEN);
    }
  }

  // Row 2: brush size (S/M/L), Clear, Save, Load
  const char *labels[6] = { "S", "M", "L", "CLR", "SAVE", "LOAD" };
  for (int i = 0; i < 6; i++) {
    int bx = i * 53 + 3;
    uint16_t btnColor = COLOR_GREY;
    if (i == 0 && currentBrushSize == 3) btnColor = COLOR_GREEN;
    if (i == 1 && currentBrushSize == 6) btnColor = COLOR_GREEN;
    if (i == 2 && currentBrushSize == 12) btnColor = COLOR_GREEN;
    if (i == 3) btnColor = COLOR_RED;
    if (i == 4) btnColor = COLOR_BLUE;
    if (i == 5) btnColor = COLOR_ORANGE;

    tft->fillRoundRect(bx, 430, 48, 34, btnColor);
    tft->drawRoundRect(bx, 430, 48, 34, COLOR_WHITE);
    tft->setTextColor(COLOR_WHITE);
    tft->setTextSize(1);
    tft->setCursor(bx + 6, 443);
    tft->print(labels[i]);
  }
}

void showDrawScreen() {
  setHardwareScrollRow(0);
  tft->fillScreen(COLOR_DARKGREY);

  // Top bar
  tft->fillRoundRect(0, 0, 320, 24, COLOR_GREY);
  tft->setTextColor(COLOR_BLACK);
  tft->setTextSize(1);
  tft->setCursor(5, 8);
  tft->print("BACK");
  tft->setCursor(140, 8);
  tft->print("PAINT");

  // Canvas
  tft->fillRect(0, PAINT_CANVAS_Y0, 320, PAINT_CANVAS_Y1 - PAINT_CANVAS_Y0, COLOR_WHITE);

  // Replay whatever is currently in drawPoints[] (e.g. after a load from SD)
  redrawDrawingFromPoints();

  drawPaintToolbar();
}

void saveDrawingToSD() {
  // Auto-generate the next free filename: /DRAW1.PNT, /DRAW2.PNT, ...
  String fname = "";
  for (int n = 1; n < 999; n++) {
    String candidate = "/DRAW" + String(n) + ".PNT";
    if (!SD.exists(candidate)) {
      fname = candidate;
      break;
    }
  }
  if (fname == "") {
    Serial.println("-> No free DRAWx.PNT slot available!");
    return;
  }

  File f = SD.open(fname, FILE_WRITE);
  if (!f) {
    Serial.println("-> Failed to open drawing file for writing!");
    return;
  }

  // Simple custom binary format: 4-byte magic, 4-byte point count, raw point records.
  f.write((const uint8_t *)"PNT1", 4);
  uint32_t count = drawPointCount;
  f.write((const uint8_t *)&count, sizeof(count));
  for (int i = 0; i < drawPointCount; i++) {
    f.write((const uint8_t *)&drawPoints[i], sizeof(DrawPoint));
  }
  f.close();

  currentDrawingFileName = fname;
  Serial.println("-> Drawing saved: " + fname);

  // Quick confirmation popup, matching the Notes app style
  tft->fillRoundRect(60, 180, 200, 80, COLOR_GREEN);
  tft->setTextColor(COLOR_WHITE);
  tft->setTextSize(2);
  tft->setCursor(95, 205);
  tft->print("SAVED");
  tft->setTextSize(1);
  tft->setCursor(80, 235);
  tft->print(fname);
  delay(900);

  showDrawScreen();  // Redraw clean, toolbar included
}

void loadDrawingFromSD(String filename) {
  String path = filename.startsWith("/") ? filename : "/" + filename;
  File f = SD.open(path);
  if (!f) {
    Serial.println("-> Could not open drawing file: " + path);
    return;
  }

  char magic[4];
  f.read((uint8_t *)magic, 4);
  if (magic[0] != 'P' || magic[1] != 'N' || magic[2] != 'T' || magic[3] != '1') {
    Serial.println("-> Not a valid drawing file: " + path);
    f.close();
    return;
  }

  uint32_t count = 0;
  f.read((uint8_t *)&count, sizeof(count));
  if (count > MAX_DRAW_POINTS) count = MAX_DRAW_POINTS;  // Safety clamp

  drawPointCount = 0;
  for (uint32_t i = 0; i < count; i++) {
    DrawPoint p;
    if (f.read((uint8_t *)&p, sizeof(DrawPoint)) != (int)sizeof(DrawPoint)) break;
    drawPoints[drawPointCount++] = p;
  }
  f.close();

  currentDrawingFileName = filename;
  lastDrawX = -1;
  lastDrawY = -1;
  Serial.println("-> Loaded drawing: " + filename + " (" + String(drawPointCount) + " points)");
}

void handleDrawTouch(int x, int y) {
  // 1. Top-left BACK chip -> Home
  if (x <= 60 && y <= 24) {
    currentScreen = STATE_HOME;
    lastDrawX = -1;
    lastDrawY = -1;
    showHomeScreen();
    delay(200);
    return;
  }

  // 2. Toolbar row 1: color palette
  if (y >= 385 && y <= 419) {
    int idx = x / 40;
    if (idx >= 0 && idx < 8) {
      currentDrawColor = paintPalette[idx];
      eraserActive = (idx == 1);  // The white swatch doubles as the eraser
      drawPaintToolbar();
    }
    return;
  }

  // 3. Toolbar row 2: brush size / clear / save / load
  if (y >= 430 && y <= 464) {
    int idx = x / 53;
    switch (idx) {
      case 0: currentBrushSize = 3; break;    // Small
      case 1: currentBrushSize = 6; break;    // Medium
      case 2: currentBrushSize = 12; break;   // Large
      case 3: clearDrawingCanvas(); break;    // Clear
      case 4: saveDrawingToSD(); return;      // Save (redraws screen itself)
      case 5:                                 // Load -> browse the Files app for a .PNT
        currentScreen = STATE_FILES;
        showFilesScreen();
        return;
    }
    drawPaintToolbar();
    return;
  }

  // 4. Canvas: draw
  if (y > PAINT_CANVAS_Y0 && y < PAINT_CANVAS_Y1) {
    uint16_t paintColor = eraserActive ? COLOR_WHITE : currentDrawColor;

    if (lastDrawX == -1) {
      tft->fillCircle(x, y, currentBrushSize / 2, paintColor);
      recordDrawPoint(x, y, paintColor, currentBrushSize, true);
    } else {
      drawInterpolatedLine(lastDrawX, lastDrawY, x, y, paintColor, currentBrushSize);
      recordDrawPoint(x, y, paintColor, currentBrushSize, false);
    }
    lastDrawX = x;
    lastDrawY = y;
  }
}


// =========================================================================
// PHOTOS (IMAGE VIEWER) APP
// =========================================================================

bool isImageFileName(String name) {
  String lower = name;
  lower.toLowerCase();
  return lower.endsWith(".bmp") || lower.endsWith(".jpg") || lower.endsWith(".jpeg") || lower.endsWith(".png");
}

// --- Uncompressed 24-bit BMP loader (no extra library required) ---
uint16_t bmpRead16(File &f) {
  uint16_t lsb = f.read();
  uint16_t msb = f.read();
  return (msb << 8) | lsb;
}

uint32_t bmpRead32(File &f) {
  uint32_t b1 = f.read();
  uint32_t b2 = f.read();
  uint32_t b3 = f.read();
  uint32_t b4 = f.read();
  return (b4 << 24) | (b3 << 16) | (b2 << 8) | b1;
}

inline uint16_t rgbTo565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

bool drawBmpFromSD(String path, int destX, int destY) {
  File bmpFile = SD.open(path);
  if (!bmpFile) {
    Serial.println("-> BMP open failed: " + path);
    return false;
  }

  if (bmpRead16(bmpFile) != 0x4D42) {  // "BM"
    Serial.println("-> Not a valid BMP file: " + path);
    bmpFile.close();
    return false;
  }

  bmpRead32(bmpFile);  // File size (unused)
  bmpRead32(bmpFile);  // Reserved fields (unused)
  uint32_t dataOffset = bmpRead32(bmpFile);
  bmpRead32(bmpFile);  // DIB header size (unused)
  int32_t bmpWidth = (int32_t)bmpRead32(bmpFile);
  int32_t bmpHeight = (int32_t)bmpRead32(bmpFile);
  bmpRead16(bmpFile);  // Color planes (always 1)
  uint16_t bmpDepth = bmpRead16(bmpFile);
  uint32_t compression = bmpRead32(bmpFile);

  if (bmpDepth != 24 || compression != 0) {
    Serial.println("-> Only uncompressed 24-bit BMP files are supported: " + path);
    bmpFile.close();
    return false;
  }

  bool flip = true;  // Standard BMPs store rows bottom-to-top
  if (bmpHeight < 0) {
    bmpHeight = -bmpHeight;
    flip = false;
  }

  int drawWidth = min((int)bmpWidth, 320 - destX);
  int drawHeight = min((int)bmpHeight, 480 - destY);

  uint32_t rowSize = ((uint32_t)bmpWidth * 3 + 3) & ~3u;  // Rows padded to 4-byte boundary
  static uint8_t rowBuffer[960];                          // Wide enough for a 320px 24-bit row
  static uint16_t lineBuf[320];

  for (int row = 0; row < drawHeight; row++) {
    int fileRow = flip ? (bmpHeight - 1 - row) : row;
    bmpFile.seek(dataOffset + (uint32_t)fileRow * rowSize);

    uint32_t bytesToRead = min((uint32_t)sizeof(rowBuffer), rowSize);
    bmpFile.read(rowBuffer, bytesToRead);

    for (int col = 0; col < drawWidth; col++) {
      uint8_t b = rowBuffer[col * 3 + 0];
      uint8_t g = rowBuffer[col * 3 + 1];
      uint8_t r = rowBuffer[col * 3 + 2];
      lineBuf[col] = rgbTo565(r, g, b);
    }
    tft->draw16bitRGBBitmap(destX, destY + row, lineBuf, drawWidth, 1);
  }

  bmpFile.close();
  return true;
}

// --- JPEG loader (requires the "JPEGDecoder" library by Bodmer) ---
bool drawJpegFromSD(String path, int destX, int destY) {
  // decodeSdFile(String) opens/parses the file itself; simplest, most version-stable call.
  if (!JpegDec.decodeSdFile(path)) {
    Serial.println("-> JPEG decode failed (unsupported file): " + path);
    return false;
  }

  uint16_t mcuWidth = JpegDec.MCUWidth;
  uint16_t mcuHeight = JpegDec.MCUHeight;

  while (JpegDec.read()) {
    uint16_t *pImg = JpegDec.pImage;

    int mcuX = destX + JpegDec.MCUx * mcuWidth;
    int mcuY = destY + JpegDec.MCUy * mcuHeight;

    // Clip the final (partial) row/column of MCUs to the true image bounds
    int winW = min((int)mcuWidth, (int)JpegDec.width - (int)(JpegDec.MCUx * mcuWidth));
    int winH = min((int)mcuHeight, (int)JpegDec.height - (int)(JpegDec.MCUy * mcuHeight));

    if (mcuX < 320 && mcuY < 480 && winW > 0 && winH > 0) {
      tft->draw16bitRGBBitmap(mcuX, mcuY, pImg, winW, winH);
    }
  }

  return true;
}

// --- PNG loader (requires the "PNGdec" library by bitbank2 / Larry Bank) ---
PNG png;
File pngFile;
int pngOffsetX = 0, pngOffsetY = 0;

void *pngOpenCB(const char *filename, int32_t *size) {
  pngFile = SD.open(filename);
  *size = pngFile ? pngFile.size() : 0;
  return &pngFile;
}
void pngCloseCB(void *handle) {
  if (pngFile) pngFile.close();
}
int32_t pngReadCB(PNGFILE *page, uint8_t *buffer, int32_t length) {
  if (!pngFile) return 0;
  return pngFile.read(buffer, length);
}
int32_t pngSeekCB(PNGFILE *page, int32_t position) {
  if (!pngFile) return 0;
  return pngFile.seek(position) ? position : -1;
}
int pngDrawCB(PNGDRAW *pDraw) {
  static uint16_t lineBuf[320];
  int w = min((int)pDraw->iWidth, 320 - pngOffsetX);
  png.getLineAsRGB565(pDraw, lineBuf, PNG_RGB565_BIG_ENDIAN, 0xFFFFFFFF);
  int destY = pngOffsetY + pDraw->y;
  if (destY >= 0 && destY < 480 && w > 0) {
    tft->draw16bitRGBBitmap(pngOffsetX, destY, lineBuf, w, 1);
  }
  return 1; // Return 1 to tell the library to continue decoding
}

bool drawPngFromSD(String path, int destX, int destY) {
  pngOffsetX = destX;
  pngOffsetY = destY;

  int rc = png.open(path.c_str(), pngOpenCB, pngCloseCB, pngReadCB, pngSeekCB, pngDrawCB);
  if (rc != PNG_SUCCESS) {
    Serial.println("-> PNG open failed: " + path);
    return false;
  }

  rc = png.decode(NULL, 0);
  png.close();

  return (rc == PNG_SUCCESS);
}

void showImageFullscreen(String filename) {
  currentImageFileName = filename;
  viewingFullscreenImage = true;

  setHardwareScrollRow(0);
  tft->fillScreen(COLOR_BLACK);

  String path = filename.startsWith("/") ? filename : "/" + filename;
  String lower = filename;
  lower.toLowerCase();

  bool ok = false;
  if (lower.endsWith(".bmp")) {
    ok = drawBmpFromSD(path, 0, 0);
  } else if (lower.endsWith(".jpg") || lower.endsWith(".jpeg")) {
    ok = drawJpegFromSD(path, 0, 0);
  } else if (lower.endsWith(".png")) {
    ok = drawPngFromSD(path, 0, 0);
  }

  if (!ok) {
    tft->setTextColor(COLOR_RED);
    tft->setTextSize(2);
    tft->setCursor(20, 220);
    tft->print("Could not display:");
    tft->setCursor(20, 245);
    tft->print(filename);
  }

  // Small chip that stays visible over any photo so BACK is always reachable
  tft->fillRoundRect(0, 0, 55, 22, COLOR_BLACK);
  tft->drawRoundRect(0, 0, 55, 22, COLOR_WHITE);
  tft->setTextColor(COLOR_WHITE);
  tft->setTextSize(1);
  tft->setCursor(6, 7);
  tft->print("BACK");
}

void showPhotosGallery() {
  viewingFullscreenImage = false;

  setHardwareScrollRow(0);
  tft->fillScreen(COLOR_BLACK);

  tft->fillRoundRect(0, 0, 320, 24, COLOR_GREY);
  tft->setTextColor(COLOR_BLACK);
  tft->setTextSize(1);
  tft->setCursor(5, 8);
  tft->print("BACK");
  tft->setCursor(130, 8);
  tft->print("PHOTOS");

  readSDCardFiles();  // Populates the shared fileList[] / totalFiles

  int currentY = 30;
  int shown = 0;
  for (int i = 0; i < totalFiles; i++) {
    if (fileList[i].isDirectory || !isImageFileName(fileList[i].name)) continue;
    if (currentY > 440) break;

    tft->fillRoundRect(10, currentY, 285, 40, COLOR_DARKGREY);
    tft->drawRoundRect(10, currentY, 285, 40, COLOR_WHITE);
    tft->setTextColor(COLOR_WHITE);
    tft->setTextSize(2);
    tft->setCursor(15, currentY + 12);
    tft->print(fileList[i].name);

    currentY += 45;
    shown++;
  }

  if (shown == 0) {
    tft->setTextColor(COLOR_GREY);
    tft->setTextSize(2);
    tft->setCursor(35, 200);
    tft->print("No images found");
    tft->setCursor(35, 230);
    tft->print("on SD card.");
  }
}

void handlePhotosTouch(int x, int y) {
  // BACK chip, top-left corner
  if (x <= 60 && y <= 24) {
    if (viewingFullscreenImage) {
      if (photosOpenedFromFiles) {
        currentScreen = STATE_FILES;
        showFilesScreen();
      } else {
        currentScreen = STATE_PHOTOS;
        showPhotosGallery();
      }
    } else {
      currentScreen = STATE_HOME;
      showHomeScreen();
    }
    delay(200);
    return;
  }

  if (viewingFullscreenImage) return;  // Ignore other taps while looking at a photo

  int rowStartY = 30;
  int idx = 0;
  for (int i = 0; i < totalFiles; i++) {
    if (fileList[i].isDirectory || !isImageFileName(fileList[i].name)) continue;

    int rowMinY = rowStartY + (idx * 45);
    int rowMaxY = rowMinY + 40;
    if (x >= 10 && x <= 295 && y >= rowMinY && y <= rowMaxY) {
      photosOpenedFromFiles = false;
      showImageFullscreen(fileList[i].name);
      return;
    }
    idx++;
  }
}


void setup() {
  
  Serial.begin(115200);
  delay(500);
  Serial.println("SETUP");

  pinMode(TOUCH_RST, OUTPUT);
  digitalWrite(TOUCH_RST, LOW);
  delay(30);
  digitalWrite(TOUCH_RST, HIGH);
  delay(150);
  pinMode(TFT_RST, OUTPUT);
  digitalWrite(TFT_RST, HIGH);
  delay(20);
  digitalWrite(TFT_RST, LOW);
  delay(50);
  digitalWrite(TFT_RST, HIGH);
  delay(150);
  pinMode(TFT_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH);
  delay(50);
  pinMode(TOUCH_INT, INPUT);

  // Initialize ESP32 3.x PWM Backlight Power
  ledcAttach(TFT_BL, 5000, 8);  // FIXED: Changed to ledcAttach to clear the red error
  ledcWrite(TFT_BL, 255);
  pinMode(27, INPUT_PULLUP);
  pinMode(17, OUTPUT);     // TFT pin power
  digitalWrite(17, HIGH);  // Set initial starting state (LOW/Off)
  delay(300);
  tft->begin(16000000);
  tft->invertDisplay(false);
  tft->fillScreen(COLOR_BLACK);
  delay(50);


  // FIXED: Fire up both dedicated memory structures
  notepadCanvas->begin();
  terminalCanvas->begin();

  Wire.begin(TOUCH_SDA, TOUCH_SCL);

  // FIXED: Fire up the mini typing canvas memory buffer
  inputCanvas->begin();
  //kbCanvas->begin();
  Wire.begin(TOUCH_SDA, TOUCH_SCL);
  delay(50);

  bus->beginWrite();
  bus->writeCommand(0x33);
  bus->write(0);
  bus->write(0);
  bus->write(1);
  bus->write(224);
  bus->write(0);
  bus->write(0);
  bus->endWrite();
  // --- 1. INITIALIZE PERMANENT STORAGE ---
  prefs.begin("phone_sys", false);  // Open namespace "phone_sys" in Read/Write mode
  // Read key "setup_done". If it doesn't exist yet, default to false.
  isSetupWizardComplete = prefs.getBool("setup_done", false);
  prefs.end();  // Close it immediately to free up system memory

  // --- NEW FILE APP STORAGE SETUP (Using IO4) ---
  // Force SD CS pin into high output mode before starting to avoid bus conflict
  pinMode(4, OUTPUT);
  digitalWrite(4, HIGH);

  Serial.println("Initializing Shared SPI SD Card Module on IO4...");
  // FIXED: Simplified syntax to clear the hidden SD compile error
  if (!SD.begin(4)) {
    Serial.println("-> SD Storage Card initialization failed!");
  } else {
    Serial.println("-> SD Storage Card initialization successful.");
  }

  // CRITICAL CALL PLACE FIX:
  // Runs your wireless network handler and checks your login variables BEFORE drawing the lockscreen canvas!
  connectToInternetRails();

  showLockScreen();
}


void loop() {
  Serial.println(ESP.getFreeHeap());
  if (isPanicActive) return;

  // FIXED: Protect the display from frame-clash overrides during snap-back animations
  if (isAnimatingLockScreen) {
    delay(5);
    return;
  }

  // ... rest of your standard loop code continues here ...

  // FIXED: Set the deadline tracker. The loop MUST complete within 200ms or crash
  loopWatchdogTimeout = millis() + 5 * 1000;
  // Place this at the very top of your void loop(), before any other screen checks:
  if (!isSetupWizardComplete) {
    if (currentScreen != STATE_SETUP) {
      currentScreen = STATE_SETUP;
      showSetupWizard();
    }
  }

  Wire.beginTransmission(FT6X36_ADDR);
  Wire.write(0x02);

  if (Wire.endTransmission() == 0) {
    Wire.requestFrom(FT6X36_ADDR, 1);

    if (Wire.available() && Wire.read() > 0) {
      uint8_t dataBuf[] = { 0, 0, 0, 0 };
      Wire.beginTransmission(FT6X36_ADDR);
      Wire.write(0x03);
      Wire.endTransmission();

      Wire.requestFrom(FT6X36_ADDR, 4);
      for (uint8_t i = 0; i < 4; i++) {
        if (Wire.available()) dataBuf[i] = Wire.read();
      }

      int rawX = ((dataBuf[0] & 0x0F) << 8) | dataBuf[1];
      int rawY = ((dataBuf[2] & 0x0F) << 8) | dataBuf[3];
      int touchX = rawX;
      int touchY = rawY;

      // Handle raw zero coordinate filtering
      if (touchX == 0 && touchY == 0) {
        if (isSliding) {
          // FIXED: Removed the instant setHardwareScrollRow(0) snap.
          // Now, the finger release transfers control to your bounce physics loop!
          handleLockTouchRelease();
        }
        if (currentScreen == STATE_DRAW) {
          lastDrawX = -1;
          lastDrawY = -1;
        }
      }


      // =========================================================================
      // MASTER APPLICATION DISPATCH LAYER (Runs only when a valid touch is active)
      // =========================================================================
      if (currentScreen == STATE_LOCKED) {
        handleLockTouch(touchX, touchY);
      } else if (currentScreen == STATE_HOME) {
        // Redirection check: If a sub-app is actively open, capture touches for the keyboard matrix parser
        if (activeSubApp == APP_NOTES || activeSubApp == APP_GOOGLE || activeSubApp == APP_TERMINAL) {

          // If you click the orange top bar back button, bounce safely out to home grid
          if (touchX >= 0 && touchX <= 60 && touchY >= 0 && touchY <= 24) {
            activeSubApp = APP_NONE;
            showHomeScreen();
            delay(300);
          } else {
            // Intercept check for your Terminal 'RUN' execute button action
            if (activeSubApp == APP_TERMINAL && touchX >= 225 && touchX <= 305 && touchY >= 420 && touchY <= 460) {
              processTerminalCommand(terminalInput);
            }
            // Otherwise, route text strings down to standard alphanumeric key registers
            else {
              parseKeyboardInput(touchX, touchY);
            }
          }
        } else {
          handleHomeTouch(touchX, touchY);
        }
      } else if (currentScreen == STATE_CALC) {
        handleCalcTouch(touchX, touchY);
      } else if (currentScreen == STATE_SETTINGS) {
        handleSettingsTouch(touchX, touchY);
      } else if (currentScreen == STATE_GAMES) {
        handleGamesTouch(touchX, touchY);
      } else if (currentScreen == STATE_FILES) {
        handleFilesTouch(touchX, touchY);
      }
      // Add this right beneath line 2147 (handleSnakeTouch) but BEFORE line 2150:
      else if (currentScreen == STATE_SETUP) {
        handleSetupTouch(touchX, touchY);
      }

      // Arcade Game Touch Drivers
      else if (currentScreen == STATE_PONG) {
        handlePongTouch(touchX, touchY);
      } else if (currentScreen == STATE_FLAPPY) {
        handleFlappyTouch(touchX, touchY);
      } else if (currentScreen == STATE_SNAKE) {
        handleSnakeTouch(touchX, touchY);
      }

      // Paint / Photos Touch Drivers
      else if (currentScreen == STATE_DRAW) {
        handleDrawTouch(touchX, touchY);
      } else if (currentScreen == STATE_PHOTOS) {
        handlePhotosTouch(touchX, touchY);
      }

    }  // Closes: if (Wire.available() && Wire.read() > 0)
    else {
      // FIXED: Moved the release detection into the correct 'else' block where the
      // touch controller confirms that your finger has physically left the glass!
      if (isSliding) {
        handleLockTouchRelease();  // Process iPhone momentum physics
      }
      if (currentScreen == STATE_DRAW) {
        // Finger lifted off the canvas: the next touch starts a brand new stroke
        // instead of drawing a line back to wherever the last stroke ended.
        lastDrawX = -1;
        lastDrawY = -1;
      }
    }

    // =========================================================================
    // CONTINUOUS ANIMATION & PHYSICS ENGINES (Runs constantly outside all touch loops)
    // =========================================================================
    if (currentScreen == STATE_SNAKE) {
      updateSnakeGame();
    } else if (currentScreen == STATE_FLAPPY) {
      updateFlappyBird();
    } else if (currentScreen == STATE_PONG) {
      updatePongGame();
    }
    if (millis() > loopWatchdogTimeout) {
      triggerRedScreenPanic("SPI BUS TIMEOUT:\nLoop execution exceeded 200ms\nCheck for infinite or nested writes.");
    }
    delay(15);
  }
}
