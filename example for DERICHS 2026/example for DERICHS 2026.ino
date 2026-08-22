/*
 * =====================================================================
 * DERICHS 2026 - ESP32-S3 SUPERMINI: ĐIỀU KHIỂN ROBOT BẰNG BÀN PHÍM USB (WIFI WEB SERIAL)
 * =====================================================================
 * - Phần cứng: ESP32-S3 SuperMini (Cổng Type-C cắm Bàn phím USB / Không dây)
 * - Truyền UART 1 chiều sang Controller 1:
 *      Chân GPIO 4 (TX1 của ESP32-S3) ---> Nối vào Chân GPIO 22 (RX2 của Controller 1)
 *      Chân GND của ESP32-S3          ---> Nối chung với Chân GND của Controller 1
 * - Tốc độ UART: 115200 Baud (0ms Latency)
 * - LED báo trạng thái: Chân GPIO 48
 * - WiFi AP:
 *      SSID: ESP32S3-USB-Bridge
 *      IP: 192.168.4.1 (Truy cập bằng điện thoại/máy tính để xem Serial log trực tuyến)
 * 
 * HUONG DAN BAT/TAT WIFI DE TOI UU HIEU NANG (WIFI POWER SAVING & PERFORMANCE):
 * Mặc định WiFi được bật để cấu hình. Khi hệ thống hoạt động ổn định thực tế,
 * ban nen TAT WiFi de tiet kiem pin, giảm nhiễu va giúp mạch chạy mượt mà nhất.
 * - Đọc cấu hình: Chỉ cần thay đổi giá trị `#define ENABLE_WIFI 1` thành `0` ở bên dưới.
 * 
 * BẢNG ÁNH XẠ NÚT BÀN PHÍM MỚI (TỐI ƯU CỰC NHẠY, KHÔNG TRÙNG PHÍM):
 *  1. HƯỚNG DI CHUYỂN (WASD):
 *     - Phím W              : Chạy TỚI ('F')
 *     - Phím S              : Chạy LÙI ('B')
 *     - Phím A              : Đi NGANG TRÁI ('L')
 *     - Phím D              : Đi NGANG PHẢI ('R')
 *     - Kết hợp chéo        : W+A, W+D, S+A, S+D
 *  2. XOAY ĐẦU XE (Q / E):
 *     - Phím Q              : Xoay đầu sang TRÁI ('A')
 *     - Phím E              : Xoay đầu sang PHẢI ('D')
 *  3. TỐC ĐỘ XE DC (1 / 2 / 3 / 4):
 *     - Phím 1, 2, 3, 4     : Tương ứng 25%, 50%, 75%, 100% tốc độ
 *  4. ĐỘNG CƠ BƯỚC STEPPER 1 (F / G - NHẤN GIỮ CHẠY, THẢ DỪNG NGAY):
 *     - Phím F              : Nâng Stepper 1 LÊN ('1')
 *     - Phím G              : Hạ Stepper 1 XUỐNG ('7')
 *  5. MODULE 2 RELAY CHÂN 33/25 (T / V - NHẤN KÍCH, THẢ TẮT NGAY):
 *     - Phím T              : Nhấn kích Relay 33 (LÊN '2'), Nhả ra hết kích (TẮT 'S')
 *     - Phím V              : Nhấn kích Relay 25 (XUỐNG '8'), Nhả ra hết kích (TẮT 'S')
 *  6. ĐIỀU KHIỂN RELAY NGOẠI VI (DẠNG TOGGLE: NHẤN BẬT -> NHẤN TẮT):
 *     - Phím SPACE          : Bật / Tắt nhóm RELAY 5, 6, 7, 8 (Có lưu trạng thái khi mất kết nối/reset)
 * 
 * HƯỚNG DẪN CHUYỂN ĐỔI SANG ESP-NOW (ESP-NOW WIRELESS CONVERSION FOR AI & DEVELOPERS):
 * Để chuyển đổi code này sang truyền thông không dây ESP-NOW (ví dụ để điều khiển robot từ xa):
 * 1. Thêm thư viện: #include <esp_now.h>
 * 2. Khai báo địa chỉ MAC của board thu nhận (Receiver MAC) và cấu trúc struct gói dữ liệu (struct DataPacket).
 * 3. Trong setup(): Khởi tạo ESP-NOW bằng `esp_now_init()`, sau đó đăng ký thiết bị nhận bằng `esp_now_add_peer()`.
 * 4. Trong sendUartPacket(): Thay thế lệnh `Serial1.printf(...)` bằng lệnh truyền không dây:
 *    `esp_now_send(robotMAC, (uint8_t *) &txData, sizeof(txData))`.
 * =====================================================================
 */

#include <Arduino.h>
#include "EspUsbHost.h"
#include <Preferences.h>

// =================================================================================
// CAU HINH TINH NANG (FEATURE CONFIGURATION)
// Dat 1 de BAT WiFi Web Terminal, dat 0 de TAT (Gup mach chay muot ma hon)
// =================================================================================
#define ENABLE_WIFI   1

#if ENABLE_WIFI
#include <WiFi.h>
#include <WebServer.h>
#endif

#define LED_PIN       48
#define S3_TX_PIN     4
#define S3_RX_PIN     5 

EspUsbHost usbHost;
Preferences preferences;

#if ENABLE_WIFI
WebServer server(80);
#endif

// Biến trạng thái toàn cục
volatile uint8_t speedDC = 25;

volatile bool relay1 = false;
volatile bool relay2 = false;
volatile bool relay3 = false;
volatile bool relay4 = false;
volatile bool relay5 = false;
volatile bool relay6 = false;
volatile bool relay7 = false;
volatile bool relay8 = false;

volatile char currentDcCmd    = 'S';
volatile char currentStepCmd  = 'S';
volatile char currentStepCmd2 = 'S';

// Biến lưu trạng thái gửi cuối cùng
char lastDcCmdSent    = 'S';
char lastStepCmdSent  = 'S';
char lastStepCmd2Sent = 'S';
uint8_t lastSpeedSent = 25;
bool lastRelay5Sent   = false;

unsigned long lastSendTime = 0;
unsigned long lastPacketTime = 0;

// Các phím Toggle chống dội
static bool lastKeySpaceState   = false;
static unsigned long lastKeySpaceTime  = 0;

// Bộ đệm cho chức năng Debug ngoài ngắt (Deferred Log)
volatile bool debugDataAvailable = false;
volatile uint8_t debugIface = 0;
volatile uint8_t debugLen = 0;
uint8_t debugBuffer[64] = {0};

volatile bool logMessageAvailable = false;
char logMessageBuffer[128] = {0};

#if ENABLE_WIFI
// Circular Buffer lưu trữ log cho Web Terminal (Tối đa 40 dòng)
#define MAX_WEB_LOGS 40
String webLogs[MAX_WEB_LOGS];
int webLogHead = 0;
unsigned long webLogSequence = 0;

void addWebLog(const String &logLine) {
  webLogs[webLogHead] = logLine;
  webLogHead = (webLogHead + 1) % MAX_WEB_LOGS;
  webLogSequence++;
}

// HTML Web Terminal lưu trong PROGMEM
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
    <span class="title">ESP32-S3 ROBOT CONTROLLER TERMINAL</span>
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

// ============================================================
// HÀM GỬI GÓI TIN QUA DÂY UART SANG CONTROLLER 1
// ============================================================
void sendUartPacket() {
  uint8_t relayByte = 0;
  if (relay1) relayByte |= (1 << 0);
  if (relay2) relayByte |= (1 << 1);
  if (relay3) relayByte |= (1 << 2);
  if (relay4) relayByte |= (1 << 3);
  if (relay5) relayByte |= (1 << 4);
  if (relay6) relayByte |= (1 << 5);
  if (relay7) relayByte |= (1 << 6);
  if (relay8) relayByte |= (1 << 7);

  Serial1.printf("$%c,%c,%c,%d,%d,%d\n",
                 currentDcCmd, currentStepCmd, currentStepCmd2,
                 speedDC, 100, relayByte);
}

// ============================================================
// GIẢI MÃ BÀN PHÍM (0ms LATENCY - KHÔNG TRÙNG PHÍM)
// ============================================================
void processKeyboardReport(uint8_t iface, const uint8_t *data, size_t len) {
  if (iface != 0) return;

  int keyStartOffset = 2; 
  int modifierOffset = 0;

  if (len >= 9 && data[0] == 0x01) {
    keyStartOffset = 3;   
    modifierOffset = 1;
  } else if (len < 8) {
    return;
  }

  bool keyW = false, keyS = false, keyA = false, keyD = false;
  bool keyQ = false, keyE = false;
  bool keyF = false, keyG = false;
  bool keyT = false, keyV = false;
  bool keySpeed1 = false, keySpeed2 = false, keySpeed3 = false, keySpeed4 = false;
  bool keySpace = false;

  bool hasAnyKey = false;

  // Quét danh sách các phím từ Byte 2 đến Byte 7
  for (int i = keyStartOffset; i < keyStartOffset + 6 && i < (int)len; i++) {
    uint8_t key = data[i];
    if (key == 0) continue;

    hasAnyKey = true;

    switch (key) {
      // HƯỚNG DI CHUYỂN
      case 0x1A: keyW = true; break; // Phím 'W' (Tiến)
      case 0x16: keyS = true; break; // Phím 'S' (Lùi)
      case 0x04: keyA = true; break; // Phím 'A' (Ngang Trái)
      case 0x07: keyD = true; break; // Phím 'D' (Ngang Phải)

      // XOAY ĐẦU XE
      case 0x14: keyQ = true; break; // Phím 'Q' (Xoay Trái)
      case 0x08: keyE = true; break; // Phím 'E' (Xoay Phải)

      // TỐC ĐỘ XE DC (1 / 2 / 3 / 4)
      case 0x1E: case 0x59: keySpeed1 = true; break; // Phím '1'
      case 0x1F: case 0x5A: keySpeed2 = true; break; // Phím '2'
      case 0x20: case 0x5B: keySpeed3 = true; break; // Phím '3'
      case 0x21: case 0x5C: keySpeed4 = true; break; // Phím '4'

      // STEPPER 1 (F / G)
      case 0x09: keyF = true; break; // Phím 'F' (Stepper Lên)
      case 0x0A: keyG = true; break; // Phím 'G' (Stepper Xuống)

      // MODULE 2 RELAY CHÂN 33/25 (T / V)
      case 0x17: keyT = true; break; // Phím 'T' (Relay 33 Lên)
      case 0x19: keyV = true; break; // Phím 'V' (Relay 25 Xuống)

      // RELAYS NGOẠI VI (SPACE)
      case 0x2C: keySpace = true; break; // Phím 'Space' (Relay 5, 6, 7, 8)
    }
  }

  // --- A. XỬ LÝ PHÍM CHỈNH TỐC ĐỘ (1, 2, 3, 4) ---
  if (keySpeed1) { speedDC = 25; }
  else if (keySpeed2) { speedDC = 50; }
  else if (keySpeed3) { speedDC = 75; }
  else if (keySpeed4) { speedDC = 100; }

  // --- B. XỬ LÝ PHÍM SPACE (TOGGLE RELAY 5, 6, 7, 8) ---
  if (keySpace) {
    if (!lastKeySpaceState && (millis() - lastKeySpaceTime > 400)) {
      lastKeySpaceTime = millis();
      bool anyOff = (!relay5 || !relay6 || !relay7 || !relay8);
      relay5 = anyOff; relay6 = anyOff; relay7 = anyOff; relay8 = anyOff;
      
      // Lưu trạng thái mới vào bộ nhớ Flash để chống mất khi reset/mất kết nối
      preferences.begin("robot", false);
      preferences.putBool("relay5", anyOff);
      preferences.end();

      snprintf(logMessageBuffer, sizeof(logMessageBuffer), "# [BÀN PHÍM] Phím SPACE -> Toggle Relay 5,6,7,8: %s\n", anyOff ? "BẬT" : "TẮT");
      logMessageAvailable = true;
    }
    lastKeySpaceState = true;
  } else {
    lastKeySpaceState = false;
  }

  // --- C. XỬ LÝ LÁI XE DC & XOAY ĐẦU (PHẢN HỒI 0MS) ---
  if (keyQ) {
    currentDcCmd = 'A'; // Q -> Xoay Trái
  } else if (keyE) {
    currentDcCmd = 'D'; // E -> Xoay Phải
  } else if (keyW && keyA) {
    currentDcCmd = 'Q'; // Chéo Tiến - Trái
  } else if (keyW && keyD) {
    currentDcCmd = 'E'; // Chéo Tiến - Phải
  } else if (keyS && keyA) {
    currentDcCmd = 'C'; // Chéo Lùi - Trái
  } else if (keyS && keyD) {
    currentDcCmd = 'N'; // Chéo Lùi - Phải
  } else if (keyW) {
    currentDcCmd = 'F'; // Tiến
  } else if (keyS) {
    currentDcCmd = 'B'; // Lùi
  } else if (keyA) {
    currentDcCmd = 'L'; // Ngang Trái
  } else if (keyD) {
    currentDcCmd = 'R'; // Ngang Phải
  } else {
    currentDcCmd = 'S'; // Dừng xe ngay lập tức khi buông phím
  }

  // --- D. XỬ LÝ STEPPER 1 (PHẢN HỒI 0MS - F/G) ---
  if (keyF) {
    currentStepCmd = '1'; // F -> Stepper Lên
  } else if (keyG) {
    currentStepCmd = '7'; // G -> Stepper Xuống
  } else {
    currentStepCmd = 'S'; // Dừng ngay lập tức khi buông phím
  }

  // --- E. XỬ LÝ MODULE 2 RELAY CHÂN 33/25 (PHẢN HỒI 0MS - T/V) ---
  if (keyT) {
    currentStepCmd2 = '2'; // T -> Kích Relay 33 (LÊN)
  } else if (keyV) {
    currentStepCmd2 = '8'; // V -> Kích Relay 25 (XUỐNG)
  } else {
    currentStepCmd2 = 'S'; // Nhả phím -> TẮT CẢ 2 RELAY ngay lập tức
  }

  // Bật / Tắt đèn LED chân 48 theo trạng thái nhấn phím
  digitalWrite(LED_PIN, hasAnyKey ? HIGH : LOW);
}

// ============================================================
// SETUP
// ============================================================
void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // Khởi tạo UART1 truyền sang Controller 1 (TX=GPIO 4, RX=GPIO 5 trống)
  Serial1.begin(115200, SERIAL_8N1, S3_RX_PIN, S3_TX_PIN);
  delay(500);

  // Khôi phục trạng thái Relay từ Flash
  preferences.begin("robot", false);
  bool savedRelay5State = preferences.getBool("relay5", false);
  relay5 = savedRelay5State; relay6 = savedRelay5State; relay7 = savedRelay5State; relay8 = savedRelay5State;
  preferences.end();

#if ENABLE_WIFI
  // Khởi tạo WiFi Access Point
  WiFi.softAP("ESP32S3-USB-Bridge");
  WiFi.setTxPower(WIFI_POWER_8_5dBm);

  // Thiết lập các route Web Server
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

  // Nháy test 3 lần lúc khởi động
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(120);
    digitalWrite(LED_PIN, LOW);
    delay(120);
  }

  // Bắt sự kiện USB HID từ Bàn phím
  usbHost.onHIDInput([](const EspUsbHostHIDInput &input) {
    if (input.length == 0 || input.data == nullptr) return;

    lastPacketTime = millis();

    // Copy sang buffer để in debug ngoài ngắt
    if (input.length <= 64) {
      debugIface = input.interfaceNumber;
      debugLen = input.length;
      memcpy(debugBuffer, input.data, input.length);
      debugDataAvailable = true;
    }

    // Giải mã phím
    processKeyboardReport(input.interfaceNumber, input.data, input.length);
  });

  usbHost.begin();
}

// ============================================================
// LOOP CHÍNH
// ============================================================
void loop() {
  unsigned long now = millis();

#if ENABLE_WIFI
  // Xử lý Web Server
  server.handleClient();
#endif

  // 1. IN LOG DEBUG ĐƯỢC TRÌ HOÃN TRONG LOOP
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

  // 2. IN TIN NHẮN ĐIỀU KHIỂN TOGGLE ĐÃ QUA LỌC TRONG LOOP
  if (logMessageAvailable) {
    logMessageAvailable = false;
    Serial1.print(logMessageBuffer);
#if ENABLE_WIFI
    addWebLog(String(logMessageBuffer));
#endif
  }

  // CƠ CHẾ CHỐNG TRÔI THỜI GIAN: Liên tục làm mới thời gian nhận tin khi có bất kỳ phím nào đang hoạt động
  bool isAnyMovingActive = (currentDcCmd != 'S') || (currentStepCmd != 'S') || (currentStepCmd2 != 'S');
  if (isAnyMovingActive) {
    lastPacketTime = now;
  }

  // 3. GỬI TỨC THÌ QUA DÂY UART KHI CÓ THAY ĐỔI LỆNH HOẶC THAY ĐỔI TRẠNG THÁI (Thread-safe)
  bool isCommandChanged = (currentDcCmd != lastDcCmdSent) ||
                          (currentStepCmd != lastStepCmdSent) ||
                          (currentStepCmd2 != lastStepCmd2Sent) ||
                          (speedDC != lastSpeedSent) ||
                          (relay5 != lastRelay5Sent);

  if (isCommandChanged) {
    sendUartPacket();
    lastDcCmdSent    = currentDcCmd;
    lastStepCmdSent  = currentStepCmd;
    lastStepCmd2Sent = currentStepCmd2;
    lastSpeedSent    = speedDC;
    lastRelay5Sent   = relay5;

    // Gửi lặp lại lệnh dừng để đảm bảo an toàn tuyệt đối
    if (currentDcCmd == 'S' && currentStepCmd == 'S' && currentStepCmd2 == 'S') {
      delay(2);
      sendUartPacket();
    }
  }

  // 4. GỬI ĐỊNH KỲ LIÊN TỤC MỖI 20MS
  if (now - lastSendTime >= 20) {
    lastSendTime = now;
    sendUartPacket();
  }

  // 5. AN TOÀN: Chỉ ngắt khi rút hẳn bàn phím quá 2000ms
  if (now - lastPacketTime > 2000) {
    digitalWrite(LED_PIN, LOW);
    currentDcCmd = 'S';
    currentStepCmd = 'S';
    currentStepCmd2 = 'S';
    sendUartPacket();
  }

  delay(2);
}
