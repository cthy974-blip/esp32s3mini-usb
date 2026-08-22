/*
 * =====================================================================
 * DERICHS 2026 - ESP32-S3 SUPERMINI: TRINH SOI MA NUT TAY CAM USB (SCANNER)
 * =====================================================================
 * GHI CHU QUAN TRONG (IMPORTANT NOTICE):
 * Code nay hoat dong chua duoc on dinh voi cac nut bam.
 * Neu muon hoat dong chinh xac, nguoi dung phai tu chinh sua rieng.
 * 
 * HUONG DAN BAT/TAT WIFI DE TOI UU HIEU NANG (WIFI POWER SAVING & PERFORMANCE):
 * Mặc định WiFi được bật để cấu hình. Khi hệ thống hoạt động ổn định thực tế,
 * ban nen TAT WiFi de tiet kiem pin, giảm nhiễu va giúp mạch chạy mượt mà nhất.
 * - Đọc cấu hình: Chỉ cần thay đổi giá trị `#define ENABLE_WIFI 1` thành `0` ở bên dưới.
 * 
 * Chuc nang:
 *  - Soi chinh xac tung Byte va tung Bit cua moi nut bam & can gat.
 *  - Khi ban bam nut nao tren tay cam, man hinh Serial Monitor (115200)
 *    hoac Web Terminal qua WiFi (192.168.4.1) se in ra chinh xac:
 *    "Interface X | Byte Y: Gia tri=Z"
 * 
 * HUONG DAN CHUYEN DOI SANG ESP-NOW (HOW TO CONVERT TO ESP-NOW FOR WIRELESS CONTROL):
 * Neu muon AI hoac lap trinh vien chuyen doi code nay de truyen lenh khong day ESP-NOW (vi du: dieu khien robot):
 * 1. Them thu vien: #include <esp_now.h>
 * 2. Khai bao dia chi MAC cua board thu (Receiver MAC) va struct goi tin dong bo (struct DataPacket).
 * 3. Trong setup(): Khoi tao ESP-NOW bang `esp_now_init()`, sau do dang ky peer bang `esp_now_add_peer()`.
 * 4. Thay the viec in Serial hoac UART bang lenh truyen khong day:
 *    `esp_now_send(receiverMAC, (uint8_t *) &txData, sizeof(txData))`.
 * =====================================================================
 */

#include <Arduino.h>
#include "EspUsbHost.h"

// =================================================================================
// CAU HINH TINH NANG (FEATURE CONFIGURATION)
// Dat 1 de BAT WiFi Web Terminal, dat 0 de TAT (Gup mach chay muot ma hon)
// =================================================================================
#define ENABLE_WIFI   1

#if ENABLE_WIFI
#include <WiFi.h>
#include <WebServer.h>
#endif

#define LED_PIN 48

EspUsbHost usbHost;

#if ENABLE_WIFI
WebServer server(80);
#endif

uint8_t idleData[4][64] = {0};
bool isIdleSet[4] = {false, false, false, false};
int idleCount[4] = {0};

// Buffer de truyen log tu ngắt ra ngoai loop de tranh xung dot
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

// HTML cua trang Web Terminal (PROGMEM)
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP32-S3 Web Gamepad Scanner</title>
  <style>
    body { background:#090d16; color:#38bdf8; font-family:'Segoe UI',monospace; margin:0; padding:15px; display:flex; flex-direction:column; height:95vh; }
    .header { background:#111827; padding:12px; border-radius:8px; margin-bottom:10px; display:flex; justify-content:space-between; align-items:center; border:1px solid #1f2937; }
    .title { font-weight:bold; color:#f9fafb; font-size:14px; }
    .status { color:#4ade80; font-size:13px; }
    .terminal { flex-grow:1; background:#030712; border:1px solid #1f2937; border-radius:8px; padding:15px; overflow-y:auto; white-space:pre-wrap; box-shadow:inset 0 4px 8px rgba(0,0,0,0.8); font-size:13px; }
    .line { margin:4px 0; line-height:1.5; border-bottom:1px solid #111827; padding-bottom:3px; }
    .button-press { color:#facc15; }
    .joystick { color:#f43f5e; }
    .info { color:#9ca3af; }
  </style>
</head>
<body>
  <div class="header">
    <span class="title">ESP32-S3 USB GAMEPAD SCANNER</span>
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
              if (l.includes('[NUT BAM]')) div.className = 'line button-press';
              else if (l.includes('[CAN GAT]')) div.className = 'line joystick';
              else div.className = 'line info';
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

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

#if ENABLE_WIFI
  // Khoi tao WiFi Access Point (AP Mode)
  WiFi.softAP("ESP32S3-USB-Bridge");
  WiFi.setTxPower(WIFI_POWER_8_5dBm); // Thiet lap cong suat phat ve 8.5 dBm de on dinh

  // Cac handler cua Web Server
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

  Serial.println("\n=======================================================");
  Serial.println("   DERICHS 2026 - TRINH SOI MA NUT TAY CAM USB");
  Serial.println("=======================================================");
  Serial.println("HUONG DAN:");
  Serial.println("1. Bam lan luot tung nut: L2, R2, L1, MODE, START, Y, A...");
  Serial.println("2. Gat can Joystick Trai: Len, Xuong, Trai, Phai");
  Serial.println("3. Gat can Joystick Phai: Len, Xuong, Trai, Phai");
  Serial.println("4. Ket noi WiFi: ESP32S3-USB-Bridge -> Web: 192.168.4.1 de xem online!");
  Serial.println("=======================================================\n");

  // Bat su kien HID
  usbHost.onHIDInput([](const EspUsbHostHIDInput &input) {
    if (input.length == 0 || input.data == nullptr) return;

    uint8_t iface = (input.interfaceNumber < 4) ? input.interfaceNumber : 0;

    // Lay mau trang thai nghi
    if (!isIdleSet[iface]) {
      for (size_t i = 0; i < input.length && i < 64; i++) {
        idleData[iface][i] = input.data[i];
      }
      idleCount[iface]++;
      if (idleCount[iface] >= 10) {
        isIdleSet[iface] = true;
        char tmp[128];
        int len = snprintf(tmp, sizeof(tmp), ">>> [OK] Da khoa trang thai nghi Interface %d (%d bytes: ", iface, (int)input.length);
        for (size_t k = 0; k < input.length && len < 120; k++) {
          len += snprintf(tmp + len, sizeof(tmp) - len, "%02X ", idleData[iface][k]);
        }
        snprintf(tmp + len, sizeof(tmp) - len, ")");
        
        // Dat vao buffer de loop in ra
        snprintf(logMessageBuffer, sizeof(logMessageBuffer), "%s\n>>> HAY BAT DAU BAM PHIM HOAC GAT CAN:\n", tmp);
        logMessageAvailable = true;
      }
      return;
    }

    // Nạp gói tin nhận được vào buffer để loop xử lý phân tích (Thread-safe)
    if (input.length <= 64 && !debugDataAvailable) {
      debugIface = input.interfaceNumber;
      debugLen = input.length;
      memcpy(debugBuffer, input.data, input.length);
      debugDataAvailable = true;
    }
  });

  usbHost.begin();
}

void loop() {
#if ENABLE_WIFI
  // Xử lý Web Server
  server.handleClient();
#endif

  // 1. Phân tích gói tin và phát hiện thay đổi (Được thực hiện trong Loop để an toàn luồng)
  if (debugDataAvailable) {
    debugDataAvailable = false;
    
    uint8_t iface = debugIface;
    bool hasChange = false;

    for (size_t i = 0; i < debugLen && i < 64; i++) {
      uint8_t base = idleData[iface][i];
      uint8_t curr = debugBuffer[i];

      // Neu la truc can gat (o trang thai nghi ~ 110..145)
      if (base >= 110 && base <= 145) {
        int diff = (int)curr - (int)base;
        if (abs(diff) > 25) {
          hasChange = true;
          digitalWrite(LED_PIN, HIGH);
          
          char logLine[128];
          snprintf(logLine, sizeof(logLine), "[CAN GAT] Interface %d | Byte %d: Gia tri=%3d (Lech: %+4d)", (int)iface, (int)i, curr, diff);
          Serial.println(logLine);
#if ENABLE_WIFI
          addWebLog(String(logLine));
#endif
          break;
        }
      }
      // Neu la nut bam
      else {
        if (curr != base) {
          hasChange = true;
          digitalWrite(LED_PIN, HIGH);
          
          char logLine[128];
          snprintf(logLine, sizeof(logLine), "[NUT BAM] Interface %d | Byte %d: 0x%02X (Khac goc: 0x%02X)", (int)iface, (int)i, curr, curr ^ base);
          Serial.println(logLine);
#if ENABLE_WIFI
          addWebLog(String(logLine));
#endif
          break;
        }
      }
    }

    if (!hasChange) {
      digitalWrite(LED_PIN, LOW);
    }
  }

  // 2. In các log thông tin khởi tạo nếu có
  if (logMessageAvailable) {
    logMessageAvailable = false;
    Serial.print(logMessageBuffer);
#if ENABLE_WIFI
    addWebLog(String(logMessageBuffer));
#endif
  }

  delay(2);
}
