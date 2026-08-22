/*
 * =================================================================================
 * DERICHS 2026 - ESP32-S3 SUPERMINI: UNIVERSAL USB KEYBOARD RECEIVER (TEMPLATE)
 * =================================================================================
 * 
 * HO CONG NGHE - HARDWARE CONNECTIONS:
 * 1. UART Simplex (Truyen tin hieu sang Vi dieu khien khac):
 *    - TX Pin: GPIO 4 (TX1 cua ESP32-S3) ---> Nôi vao chan RX cua MCU nhan
 *    - GND Pin: GND cua ESP32-S3          ---> Nôi chung GND voi MCU nhan
 * 2. Status LED Indicator:
 *    - LED Pin: GPIO 48 (LED tich hop tren bo mach SuperMini)
 * 3. Unused Pins:
 *    - RX Pin: GPIO 5 (Gan lam chan nhan ao de khoi tao UART safely, khong noi day)
 * 
 * 4. WiFi AP Config (Doc Serial qua Web):
 *    - SSID: ESP32S3-USB-Bridge
 *    - IP Address: 192.168.4.1 (Truy cap qua trinh duyet Web tren dien thoai/may tinh)
 * 
 * ---------------------------------------------------------------------------------
 * HUONG DAN DANH CHO LAP TRINH VIEN VA AI (DEVELOPER & AI INSTRUCTIONS):
 * Code nay la khung nguon mau (Template) de doc phim tu Ban phim USB qua cong Type-C
 * va truyen lenh sang thiet bi khac qua UART hoac WiFi.
 * 
 * HUONG DAN BAT/TAT WIFI DE TOI UU HIEU NANG (WIFI POWER SAVING & PERFORMANCE):
 * Mac dinh WiFi duoc bat de cau hinh. Khi he thong hoat dong on dinh thuc te,
 * ban nen TAT WiFi de tiet kiem pin, giam nhieu va giup mach chay muot ma nhat.
 * - De tat/bat: Chi can thay doi gia tri `#define ENABLE_WIFI 1` thanh `0` o ben duoi.
 * 
 * Cac buoc de them nut va lenh moi:
 * - Buoc 1: Khai bao ma quet phim tai "PHAN A: KHAI BAO MA PHIM".
 * - Buoc 2: Khai bao ma ky tu lenh UART tai "PHAN B: MA LENH UART".
 * - Buoc 3: Tao bien trang thai logic tai "PHAN C: BIEN TRANG THAI".
 * - Buoc 4: Them quy trinh doc va toggle phim tai "PHAN D: LOGIC NHAN PHIM".
 * - Buoc 5: Cap nhat goi tin truyen tai "PHAN E: HAM GUI UART / HANH DONG".
 * 
 * HUONG DAN CHUYEN DOI SANG ESP-NOW (HOW TO CONVERT UART TO ESP-NOW FOR WIRELESS CONTROL):
 * Neu muon AI hoac lap trinh vien chuyen doi code nay tu UART sang truyen khong day ESP-NOW:
 * 1. Them thu vien: #include <esp_now.h>
 * 2. Khai bao dia chi MAC cua board thu (Receiver MAC) va struct data dong bo.
 * 3. Trong setup(): Khoi tao ESP-NOW bang `esp_now_init()`, sau do dang ky thiet bi thu 
 *    bang `esp_now_add_peer()`.
 * 4. Thay the viec gui `Serial1.printf(...)` bang lenh truyen khong day:
 *    `esp_now_send(receiverMAC, (uint8_t *) &txData, sizeof(txData))`.
 * ---------------------------------------------------------------------------------
 */

#include <Arduino.h>
#include "EspUsbHost.h"

// =================================================================================
// CAU HINH TINH NANG (FEATURE CONFIGURATION)
// Dat 1 de BAT WiFi Web Terminal, dat 0 de TAT (Giup mach chay muot ma hon)
// =================================================================================
#define ENABLE_WIFI   1

#if ENABLE_WIFI
#include <WiFi.h>
#include <WebServer.h>
#endif

// Dinh nghia Pin phan cung
#define LED_PIN       48
#define S3_TX_PIN     4
#define S3_RX_PIN     5 

// =================================================================================
// PHAN A: KHAI BAO MA PHIM (USB HID SCAN CODES)
// Định nghĩa các mã quét USB HID cho các phím bạn muốn sử dụng.
// =================================================================================
const uint8_t KEY_EXAMPLE_A = 0x04; // Phim 'A' (Vi du: Phim Momentary - Nhan giu)
const uint8_t KEY_EXAMPLE_B = 0x05; // Phim 'B' (Vi du: Phim Toggle - Bat/Tat)

// [AI PLACEHOLDER - Them ma phim moi tai day]
// Vi du: const uint8_t KEY_NEW = 0x1A; // Phim 'W'

// =================================================================================
// PHAN B: MA LENH UART (UART PROTOCOL CHARACTERS)
// Định nghĩa ký tự lệnh gửi sang thiet bi nhan.
// =================================================================================
const char CMD_EXAMPLE_A_ON  = '1';
const char CMD_EXAMPLE_A_OFF = '0';
const char CMD_EXAMPLE_B_ON  = 'Y';
const char CMD_EXAMPLE_B_OFF = 'N';

// [AI PLACEHOLDER - Them ky tu lenh UART moi tai day]

// =================================================================================
// PHAN C: BIEN TRANG THAI (STATE VARIABLES)
// =================================================================================
EspUsbHost usbHost;

#if ENABLE_WIFI
WebServer server(80);
#endif

// Bien trang thai cho phim vi du A (Nhan giu)
volatile bool stateActionA = false; 

// Bien trang thai cho phim vi du B (Toggle Bat/Tat)
volatile bool stateActionB = false; 
static bool lastStateKeyB  = false;
static unsigned long lastDebounceTimeB = 0;

// [AI PLACEHOLDER - Them bien trang thai va bien chong doi moi tai day]

// Bien dong bo truyen du lieu UART
unsigned long lastSendTime = 0;
volatile bool debugDataAvailable = false;
volatile uint8_t debugIface = 0;
volatile uint8_t debugLen = 0;
uint8_t debugBuffer[64] = {0};

volatile bool logMessageAvailable = false;
char logMessageBuffer[128] = {0};

#if ENABLE_WIFI
// Circular Buffer luu tru log cho Web Terminal (Toi da 40 dong)
#define MAX_WEB_LOGS 40
String webLogs[MAX_WEB_LOGS];
int webLogHead = 0;
unsigned long webLogSequence = 0;

void addWebLog(const String &logLine) {
  webLogs[webLogHead] = logLine;
  webLogHead = (webLogHead + 1) % MAX_WEB_LOGS;
  webLogSequence++;
}

// HTML cua trang Web Terminal (Luu vao PROGMEM de tiet kiem RAM)
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP32-S3 Web Serial Terminal</title>
  <style>
    body { background:#090d16; color:#4ade80; font-family:'Segoe UI',monospace; margin:0; padding:15px; display:flex; flex-direction:column; height:95vh; }
    .header { background:#111827; padding:12px; border-radius:8px; margin-bottom:10px; display:flex; justify-content:space-between; align-items:center; border:1px solid #1f2937; }
    .title { font-weight:bold; color:#f9fafb; font-size:14px; }
    .status { color:#38bdf8; font-size:13px; }
    .terminal { flex-grow:1; background:#030712; border:1px solid #1f2937; border-radius:8px; padding:15px; overflow-y:auto; white-space:pre-wrap; box-shadow:inset 0 4px 8px rgba(0,0,0,0.8); font-size:13px; }
    .line { margin:4px 0; line-height:1.5; border-bottom:1px solid #111827; padding-bottom:3px; }
    .keyboard { color:#facc15; }
    .debug { color:#9ca3af; }
  </style>
</head>
<body>
  <div class="header">
    <span class="title">ESP32-S3 USB BRIDGE TERMINAL</span>
    <span class="status">AP: ESP32S3-USB-Bridge (192.168.4.1)</span>
  </div>
  <div class="terminal" id="term">Connecting to ESP32-S3...</div>
  <script>
    let lastSeq = 0;
    const term = document.getElementById('term');
    function fetchLogs() {
      fetch('/get-logs?last=' + lastSeq)
        .then(r => r.json())
        .then(data => {
          if(data.logs && data.logs.length > 0) {
            if(lastSeq === 0) term.innerHTML = '';
            data.logs.forEach(l => {
              const div = document.createElement('div');
              div.className = 'line' + (l.startsWith('#') ? ' keyboard' : ' debug');
              div.textContent = l;
              term.appendChild(div);
            });
            term.scrollTop = term.scrollHeight;
          }
          lastSeq = data.seq;
        })
        .catch(e => console.error(e));
    }
    setInterval(fetchLogs, 250);
  </script>
</body>
</html>
)rawliteral";
#endif

// =================================================================================
// PHAN E: HAM GUI UART / HANH DONG (sendDataToUart)
// =================================================================================
void sendDataToUart() {
  // Day la noi ban dinh dang goi tin va gui sang vi dieu khien khac qua UART.
  Serial1.printf("$%c,%c\n", 
                 stateActionA ? CMD_EXAMPLE_A_ON : CMD_EXAMPLE_A_OFF,
                 stateActionB ? CMD_EXAMPLE_B_ON : CMD_EXAMPLE_B_OFF);
}

// =================================================================================
// PHAN D: LOGIC NHAN PHIM (processKeyboardReport)
// =================================================================================
void processKeyboardReport(uint8_t iface, const uint8_t *data, size_t len) {
  if (iface != 0) return;

  int keyStartOffset = 2; 
  if (len >= 9 && data[0] == 0x01) {
    keyStartOffset = 3;   
  } else if (len < 8) {
    return;
  }

  bool isPressedKeyA = false;
  bool isPressedKeyB = false;

  // [AI PLACEHOLDER - Khai bao co phim moi tai day]

  bool hasAnyKey = false;

  for (int i = keyStartOffset; i < keyStartOffset + 6 && i < (int)len; i++) {
    uint8_t key = data[i];
    if (key == 0) continue;

    hasAnyKey = true;

    if (key == KEY_EXAMPLE_A) {
      isPressedKeyA = true;
    }
    else if (key == KEY_EXAMPLE_B) {
      isPressedKeyB = true;
    }
    
    // [AI PLACEHOLDER - Map phim scan code tai day]
  }

  // --- VI DU 1: LOGIC NHAN GIU (MOMENTARY ACTION - PHIM A) ---
  if (isPressedKeyA) {
    stateActionA = true;
  } else {
    stateActionA = false;
  }

  // --- VI DU 2: LOGIC TOGGLE BAT/TAT (TOGGLE ACTION - PHIM B) ---
  if (isPressedKeyB) {
    if (!lastStateKeyB && (millis() - lastDebounceTimeB > 400)) {
      lastDebounceTimeB = millis();
      stateActionB = !stateActionB;
      
      snprintf(logMessageBuffer, sizeof(logMessageBuffer), "# [LOG] Phim B (Toggle) -> Trang thai: %s\n", stateActionB ? "ON" : "OFF");
      logMessageAvailable = true;
    }
    lastStateKeyB = true;
  } else {
    lastStateKeyB = false;
  }

  // [AI PLACEHOLDER - Them logic xu ly phim tai day]

  digitalWrite(LED_PIN, hasAnyKey ? HIGH : LOW);
}

// =================================================================================
// SETUP & WEBSERVER HANDLERS
// =================================================================================
void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // Khoi tao UART truyen sang MCU khac (Baud 115200)
  Serial1.begin(115200, SERIAL_8N1, S3_RX_PIN, S3_TX_PIN);
  delay(500);

#if ENABLE_WIFI
  // Khoi tao WiFi Access Point (AP Mode)
  WiFi.softAP("ESP32S3-USB-Bridge");
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  
  // Khai bao cac duong dan Web Server
  server.on("/", []() {
    server.send_P(200, "text/html", INDEX_HTML);
  });

  server.on("/get-logs", []() {
    unsigned long clientSeq = 0;
    if (server.hasArg("last")) {
      clientSeq = server.arg("last").toInt();
    }

    String json = "{\"seq\":" + String(webLogSequence) + ",\"logs\":[";
    
    if (clientSeq < webLogSequence) {
      unsigned long missed = webLogSequence - clientSeq;
      if (missed > MAX_WEB_LOGS) missed = MAX_WEB_LOGS;

      int startIdx = (webLogHead - missed + MAX_WEB_LOGS) % MAX_WEB_LOGS;
      for (unsigned long i = 0; i < missed; i++) {
        int idx = (startIdx + i) % MAX_WEB_LOGS;
        String esc = webLogs[idx];
        esc.replace("\"", "\\\"");
        esc.replace("\n", "");
        esc.replace("\r", "");
        json += "\"" + esc + "\"";
        if (i < missed - 1) json += ",";
      }
    }
    json += "]}";
    server.send(200, "application/json", json);
  });

  server.begin();
#endif

  // Nháy báo hiệu thành công 3 lần
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(120);
    digitalWrite(LED_PIN, LOW);
    delay(120);
  }

  // Dang ky USB HID Input callback
  usbHost.onHIDInput([](const EspUsbHostHIDInput &input) {
    if (input.length == 0 || input.data == nullptr) return;

    if (input.length <= 64) {
      debugIface = input.interfaceNumber;
      debugLen = input.length;
      memcpy(debugBuffer, input.data, input.length);
      debugDataAvailable = true;
    }

    processKeyboardReport(input.interfaceNumber, input.data, input.length);
  });

  usbHost.begin();
}

// =================================================================================
// LOOP CHINH
// =================================================================================
void loop() {
  unsigned long now = millis();

#if ENABLE_WIFI
  // Xu ly Web Server Client
  server.handleClient();
#endif

  // 1. In debug HID tho & gui len Web Ring Buffer
  if (debugDataAvailable) {
    debugDataAvailable = false;
    char tmp[128];
    int len = snprintf(tmp, sizeof(tmp), "KB Iface %d | len=%d | Data: ", debugIface, debugLen);
    for (int k = 0; k < debugLen && len < 120; k++) {
      len += snprintf(tmp + len, sizeof(tmp) - len, "%02X ", debugBuffer[k]);
    }
    Serial1.println(tmp);
#if ENABLE_WIFI
    addWebLog(String(tmp));
#endif
  }

  // 2. In log tu ban phim & gui len Web Ring Buffer
  if (logMessageAvailable) {
    logMessageAvailable = false;
    Serial1.print(logMessageBuffer);
#if ENABLE_WIFI
    addWebLog(String(logMessageBuffer));
#endif
  }

  // 3. Gui dinh ky moi 20ms
  if (now - lastSendTime >= 20) {
    lastSendTime = now;
    sendDataToUart();
  }

  delay(2);
}
