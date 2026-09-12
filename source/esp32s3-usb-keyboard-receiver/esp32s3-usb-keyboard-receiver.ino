/*
 * =================================================================================
 * DERICHS 2026 - ESP32-S3 SUPERMINI: UNIVERSAL USB KEYBOARD RECEIVER (TEMPLATE)
 * =================================================================================
 * 
 * HO CONG NGHE - HARDWARE CONNECTIONS:
 * 1. UART Simplex (Truyen tin hieu sang Vi dieu khien khac):
 *    - TX Pin: GPIO 4 (TX1 cua ESP32-S3) ---> Nối vào chân RX của MCU nhận
 *    - GND Pin: GND cua ESP32-S3          ---> Nối chung GND với MCU nhận
 * 2. Status LED Indicator:
 *    - LED Pin: GPIO 48 (LED đỏ đơn cạnh cổng Type-C - Sáng khi gõ phím, tự tắt sau 120ms)
 * 3. Unused Pins:
 *    - RX Pin: GPIO 5 (Gán làm chân ảo để khởi tạo UART safely, không nối dây)
 * 
 * 4. WiFi AP Config (Doc Serial qua Web & Keyboard Dashboard):
 *    - SSID: ESP32S3-USB-Bridge
 *    - IP Address: 192.168.4.1 (Truy cập qua trình duyệt Web trên điện thoại/máy tính)
 * 
 * ---------------------------------------------------------------------------------
 * HUONG DAN DANH CHO LAP TRINH VIEN VA AI (DEVELOPER & AI INSTRUCTIONS):
 * Code nay la khung nguon mau (Template) de doc BAN PHIM USB qua cong Type-C.
 * Giao dien Web gom 2 phan: Phia tren la Ban phim ao Tieng Anh truoc quan + Khung text go phim,
 * Phia duoi la Terminal Log cu.
 * 
 * HUONG DAN BAT/TAT WIFI DE TOI UU HIEU NANG (WIFI POWER SAVING & PERFORMANCE):
 * Mặc định WiFi được bật để hiển thị giao diện Web Dashboard. Khi hoạt động thực tế,
 * bạn nên TẮT WiFi để tiết kiệm pin, giảm nhiễu và giúp mạch chạy mượt mà nhất.
 * - Để tắt/bật: Chỉ cần thay đổi giá trị `#define ENABLE_WIFI 1` thành `0` ở bên dưới.
 * ---------------------------------------------------------------------------------
 */

#include <Arduino.h>
#include "EspUsbHost.h"

// =================================================================================
// CAU HINH TINH NANG (FEATURE CONFIGURATION)
// Dat 1 de BAT WiFi Web Dashboard, dat 0 de TAT (Giup mach chay muot ma hon)
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
// HAM DIEU KHIEN LED DO DON CANH CONG TYPE-C (PIN 48)
// =================================================================================
inline void setBoardLED(bool state) {
  digitalWrite(LED_PIN, state ? HIGH : LOW);
}

volatile unsigned long lastKeyActionTime = 0;
volatile bool ledActive = false;

inline void triggerKeyLED() {
  lastKeyActionTime = millis();
  if (!ledActive) {
    ledActive = true;
    digitalWrite(LED_PIN, HIGH);
  }
}

// =================================================================================
// PHAN A: KHAI BAO MA PHIM & BIEN TRANG THAI (STATE VARIABLES)
// =================================================================================
const uint8_t KEY_EXAMPLE_A = 0x04; // Phim 'A' (Vi du: Phim Momentary - Nhan giu)
const uint8_t KEY_EXAMPLE_B = 0x05; // Phim 'B' (Vi du: Phim Toggle - Bat/Tat)

const char CMD_EXAMPLE_A_ON  = '1';
const char CMD_EXAMPLE_A_OFF = '0';
const char CMD_EXAMPLE_B_ON  = 'Y';
const char CMD_EXAMPLE_B_OFF = 'N';

EspUsbHost usbHost;

#if ENABLE_WIFI
WebServer server(80);
#endif

// Bien trang thai Bàn phím
volatile bool stateActionA = false; 
volatile bool stateActionB = false; 
static bool lastStateKeyB  = false;
static unsigned long lastDebounceTimeB = 0;

// Danh sach cac phim dang duoc nhan (Luu toi da 6 phim)
volatile uint8_t activeKeyCodes[6] = {0};
volatile uint8_t activeKeyCount = 0;

// Khung Text chua van ban go Tieng Anh
char typedTextBuffer[256] = {0};
int typedTextLen = 0;
static uint8_t lastProcessedKey = 0;
static unsigned long lastKeyRepeatTime = 0;

// Bien dong bo truyen du lieu UART & Log
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

// HTML & GIAO DIEN WEB (KHUNG BAN PHIM AO TIENG ANH + KHUNG GO TEXT + TERMINAL LOG CU)
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP32-S3 USB Keyboard Dashboard</title>
  <style>
    * { box-sizing: border-box; }
    body { background:#090d16; color:#4ade80; font-family:'Segoe UI',monospace; margin:0; padding:12px; display:flex; flex-direction:column; height:98vh; }
    
    .header { background:#111827; padding:10px 14px; border-radius:8px; margin-bottom:10px; display:flex; justify-content:space-between; align-items:center; border:1px solid #1f2937; }
    .title { font-weight:bold; color:#f9fafb; font-size:14px; }
    .status { color:#38bdf8; font-size:12px; }

    /* NUT BAM TOGGLE GIAO DIEN TRUC QUAN */
    .toggle-btn { width:100%; background:#1e293b; color:#38bdf8; border:1px solid #334155; padding:10px; border-radius:8px; font-weight:bold; font-size:13px; cursor:pointer; text-align:center; margin-bottom:10px; transition:all 0.2s; }
    .toggle-btn:hover { background:#334155; color:#fff; }
    
    /* PHAN GIAO DIEN TRUC QUAN (BAN DAU AN) */
    .visual-container { display:none; background:#0f172a; border:1px solid #1e293b; border-radius:8px; padding:12px; margin-bottom:10px; flex-direction:column; gap:10px; }
    .visual-container.open { display:flex; }
    
    /* KHUNG TEXT GO CHU TIENG ANH */
    .text-display-box { background:#020617; border:1px solid #334155; border-radius:6px; padding:10px 14px; min-height:48px; font-size:16px; color:#38bdf8; word-break:break-all; font-weight:bold; box-shadow:inset 0 2px 4px rgba(0,0,0,0.6); }
    .text-label { font-size:11px; color:#9ca3af; margin-bottom:4px; text-transform:uppercase; font-weight:bold; }

    /* BAN PHIM AO QWERTY TU DONG RE-SCALE DE KHONG BI KHUAT RIIA */
    .keyboard-grid { display:flex; flex-direction:column; gap:4px; background:#020617; border:1px solid #1e293b; border-radius:8px; padding:8px 4px; width:100%; box-sizing:border-box; overflow-x:auto; }
    .kb-row { display:flex; gap:3px; width:100%; max-width:720px; margin:0 auto; box-sizing:border-box; }
    .key-cap { flex:1; min-width:0; height:32px; background:#1e293b; color:#94a3b8; border:1px solid #334155; border-radius:4px; display:flex; align-items:center; justify-content:center; font-size:11px; font-weight:bold; transition:all 0.08s; user-select:none; white-space:nowrap; overflow:hidden; text-overflow:ellipsis; padding:0 1px; }
    .key-cap.wide-1 { flex:1.4; }
    .key-cap.wide-2 { flex:1.8; }
    .key-cap.space-bar { flex:6.5; }
    
    /* NHAY SANG KHI NHAN PHIM THAT */
    .key-cap.active-key { background:#16a34a !important; color:#ffffff !important; border-color:#22c55e !important; box-shadow:0 0 10px #22c55e; transform:translateY(1px); }

    /* TERMINAL LOG MAN HINH CU (MAC DINH HIENTHI MAIN) */
    .terminal { flex-grow:1; background:#030712; border:1px solid #1f2937; border-radius:8px; padding:12px; overflow-y:auto; white-space:pre-wrap; font-size:12px; }
    .line { margin:3px 0; line-height:1.4; border-bottom:1px solid #111827; padding-bottom:2px; }
    .keyboard-log { color:#facc15; }
    .debug-log { color:#9ca3af; }
  </style>
</head>
<body>
  <div class="header">
    <span class="title">ESP32-S3 USB KEYBOARD RECEIVER</span>
    <span class="status">AP: ESP32S3-USB-Bridge (192.168.4.1)</span>
  </div>

  <!-- NUT BAM BAT/TAT GIAO DIEN TRUC QUAN -->
  <div class="toggle-btn" id="btn-toggle" onclick="toggleVisual()">
    [ MO GIAO DIEN BAN PHIM AO & KHUNG GO CHU TRUC QUAN ]
  </div>

  <!-- KHUNG TRUC QUAN (KHUNG GO TEXT + BAN PHIM AO) -->
  <div class="visual-container" id="visual-panel">
    <div>
      <div class="text-label">Live Typed Text (English):</div>
      <div class="text-display-box" id="typed-box">Waiting for typing...</div>
    </div>

    <!-- BAN PHIM AO US QWERTY -->
    <div class="keyboard-grid">
      <!-- ROW 1: NUMBERS -->
      <div class="kb-row">
        <div class="key-cap" id="k-41">Esc</div>
        <div class="key-cap" id="k-30">1</div><div class="key-cap" id="k-31">2</div><div class="key-cap" id="k-32">3</div>
        <div class="key-cap" id="k-33">4</div><div class="key-cap" id="k-34">5</div><div class="key-cap" id="k-35">6</div>
        <div class="key-cap" id="k-36">7</div><div class="key-cap" id="k-37">8</div><div class="key-cap" id="k-38">9</div>
        <div class="key-cap" id="k-39">0</div><div class="key-cap" id="k-45">-</div><div class="key-cap" id="k-46">=</div>
        <div class="key-cap wide-1" id="k-42">Backspace</div>
      </div>
      <!-- ROW 2: QWERTY -->
      <div class="kb-row">
        <div class="key-cap wide-1" id="k-43">Tab</div>
        <div class="key-cap" id="k-20">Q</div><div class="key-cap" id="k-26">W</div><div class="key-cap" id="k-8">E</div>
        <div class="key-cap" id="k-21">R</div><div class="key-cap" id="k-23">T</div><div class="key-cap" id="k-28">Y</div>
        <div class="key-cap" id="k-24">U</div><div class="key-cap" id="k-12">I</div><div class="key-cap" id="k-18">O</div>
        <div class="key-cap" id="k-19">P</div><div class="key-cap" id="k-47">[</div><div class="key-cap" id="k-48">]</div>
      </div>
      <!-- ROW 3: ASDFGH -->
      <div class="kb-row">
        <div class="key-cap wide-1" id="k-57">Caps</div>
        <div class="key-cap" id="k-4">A</div><div class="key-cap" id="k-22">S</div><div class="key-cap" id="k-7">D</div>
        <div class="key-cap" id="k-9">F</div><div class="key-cap" id="k-10">G</div><div class="key-cap" id="k-11">H</div>
        <div class="key-cap" id="k-13">J</div><div class="key-cap" id="k-14">K</div><div class="key-cap" id="k-15">L</div>
        <div class="key-cap" id="k-51">;</div><div class="key-cap" id="k-52">'</div>
        <div class="key-cap wide-2" id="k-40">Enter</div>
      </div>
      <!-- ROW 4: ZXCVBN -->
      <div class="kb-row">
        <div class="key-cap wide-2" id="k-225">Shift</div>
        <div class="key-cap" id="k-29">Z</div><div class="key-cap" id="k-27">X</div><div class="key-cap" id="k-6">C</div>
        <div class="key-cap" id="k-25">V</div><div class="key-cap" id="k-5">B</div><div class="key-cap" id="k-17">N</div>
        <div class="key-cap" id="k-16">M</div><div class="key-cap" id="k-54">,</div><div class="key-cap" id="k-55">.</div>
        <div class="key-cap" id="k-56">/</div>
        <div class="key-cap wide-2" id="k-229">Shift</div>
      </div>
      <!-- ROW 5: SPACEBAR -->
      <div class="kb-row">
        <div class="key-cap" id="k-224">Ctrl</div><div class="key-cap" id="k-226">Alt</div>
        <div class="key-cap space-bar" id="k-44">Space</div>
        <div class="key-cap" id="k-230">Alt</div><div class="key-cap" id="k-228">Ctrl</div>
      </div>
    </div>
  </div>

  <!-- TERMINAL LOG MAN HINH CU (MAC DINH HIENTHI MAIN) -->
  <div class="terminal" id="term">Connecting to ESP32-S3...</div>

  <script>
    let lastSeq = 0;
    const term = document.getElementById('term');
    const typedBox = document.getElementById('typed-box');
    const visualPanel = document.getElementById('visual-panel');
    const btnToggle = document.getElementById('btn-toggle');

    function toggleVisual() {
      if (visualPanel.classList.contains('open')) {
        visualPanel.classList.remove('open');
        btnToggle.textContent = '[ MO GIAO DIEN BAN PHIM AO & KHUNG GO CHU TRUC QUAN ]';
      } else {
        visualPanel.classList.add('open');
        btnToggle.textContent = '[ DONG GIAO DIEN TRUC QUAN ]';
      }
    }

    function fetchLogs() {
      fetch('/get-logs?last=' + lastSeq)
        .then(r => r.json())
        .then(data => {
          // Cập nhật khung chữ Tiếng Anh đã gõ
          if (data.typed !== undefined) {
            typedBox.textContent = data.typed.length > 0 ? data.typed : '(Chưa gõ ký tự nào...)';
          }

          // Cập nhật các phím đang được nhấn sáng bàn phím ảo
          document.querySelectorAll('.key-cap').forEach(el => el.classList.remove('active-key'));
          if (data.keys && data.keys.length > 0) {
            data.keys.forEach(code => {
              const el = document.getElementById('k-' + code);
              if (el) el.classList.add('active-key');
            });
          }

          // Cập nhật Terminal Log cũ
          if(data.logs && data.logs.length > 0) {
            if(lastSeq === 0) term.innerHTML = '';
            data.logs.forEach(l => {
              const div = document.createElement('div');
              div.className = l.startsWith('#') ? 'line keyboard-log' : 'line debug-log';
              div.textContent = l;
              term.appendChild(div);
            });
            term.scrollTop = term.scrollHeight;
          }
          lastSeq = data.seq;
        })
        .catch(e => console.error(e));
    }
    setInterval(fetchLogs, 150);
  </script>
</body>
</html>
)rawliteral";
#endif

// =================================================================================
// PHAN B: HAM GUI UART (sendDataToUart)
// =================================================================================
void sendDataToUart() {
  Serial1.printf("$%c,%c\n", 
                 stateActionA ? CMD_EXAMPLE_A_ON : CMD_EXAMPLE_A_OFF,
                 stateActionB ? CMD_EXAMPLE_B_ON : CMD_EXAMPLE_B_OFF);
}

// =================================================================================
// CHUYỂN ĐỔI SCANCODE SANG KÝ TỰ TIẾNG ANH (US QWERTY ASCII)
// =================================================================================
char hidCodeToEnglishAscii(uint8_t keycode, bool shift) {
  if (keycode >= 0x04 && keycode <= 0x1D) { // Phím A - Z
    char base = 'a' + (keycode - 0x04);
    if (shift) return base - 32; // In hoa A-Z
    return base;
  }
  if (keycode >= 0x1E && keycode <= 0x27) { // Phím 1..9, 0
    const char numLow[]   = "1234567890";
    const char numShift[] = "!@#$%^&*()";
    uint8_t idx = (keycode == 0x27) ? 9 : (keycode - 0x1E);
    return shift ? numShift[idx] : numLow[idx];
  }
  if (keycode == 0x2C) return ' ';  // Space
  if (keycode == 0x28) return '\n'; // Enter
  if (keycode == 0x2A) return '\b'; // Backspace
  if (keycode == 0x36) return shift ? '<' : ',';
  if (keycode == 0x37) return shift ? '>' : '.';
  if (keycode == 0x38) return shift ? '?' : '/';
  if (keycode == 0x2E) return shift ? '+' : '=';
  if (keycode == 0x2D) return shift ? '_' : '-';
  return 0;
}

// =================================================================================
// PHAN D: LOGIC NHAN PHIM (processKeyboardReport)
// =================================================================================
void processKeyboardReport(uint8_t iface, const uint8_t *data, size_t len) {
  if (iface != 0 && len != 8) return;

  int keyStartOffset = 2; 
  uint8_t modifiers = data[0];

  if (len >= 9 && data[0] == 0x01) {
    keyStartOffset = 3;   
    modifiers = data[1];
  } else if (len < 8) {
    return;
  }

  bool shiftActive = (modifiers & 0x22) != 0; // Shift Trái hoặc Shift Phải
  bool isPressedKeyA = false;
  bool isPressedKeyB = false;

  activeKeyCount = 0;
  bool hasAnyKey = false;
  uint8_t currentFirstKey = 0;

  for (int i = keyStartOffset; i < keyStartOffset + 6 && i < (int)len; i++) {
    uint8_t key = data[i];
    if (key == 0) continue;

    hasAnyKey = true;
    if (currentFirstKey == 0) currentFirstKey = key;

    if (activeKeyCount < 6) {
      activeKeyCodes[activeKeyCount++] = key;
    }

    if (key == KEY_EXAMPLE_A) isPressedKeyA = true;
    else if (key == KEY_EXAMPLE_B) isPressedKeyB = true;
  }

  // Thêm các phím Shift/Ctrl/Alt vào danh sách active nếu có nhấn
  if (modifiers & 0x01) if (activeKeyCount < 6) activeKeyCodes[activeKeyCount++] = 224; // Left Ctrl
  if (modifiers & 0x02) if (activeKeyCount < 6) activeKeyCodes[activeKeyCount++] = 225; // Left Shift
  if (modifiers & 0x04) if (activeKeyCount < 6) activeKeyCodes[activeKeyCount++] = 226; // Left Alt
  if (modifiers & 0x20) if (activeKeyCount < 6) activeKeyCodes[activeKeyCount++] = 229; // Right Shift

  // Gõ ký tự Tiếng Anh vào khung chữ (Live Typed Text)
  if (currentFirstKey != 0 && (currentFirstKey != lastProcessedKey || millis() - lastKeyRepeatTime > 300)) {
    lastProcessedKey = currentFirstKey;
    lastKeyRepeatTime = millis();

    char ch = hidCodeToEnglishAscii(currentFirstKey, shiftActive);
    if (ch == '\b') { // Backspace xóa ký tự cuối
      if (typedTextLen > 0) {
        typedTextBuffer[--typedTextLen] = 0;
      }
    } else if (ch != 0) { // Chèn ký tự mới
      if (typedTextLen < 250) {
        typedTextBuffer[typedTextLen++] = ch;
        typedTextBuffer[typedTextLen] = 0;
      }
    }
  } else if (currentFirstKey == 0) {
    lastProcessedKey = 0;
  }

  // --- VI DU 1: MOMENTARY ACTION (PHIM A) ---
  stateActionA = isPressedKeyA;

  // --- VI DU 2: TOGGLE ACTION (PHIM B) ---
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

  if (hasAnyKey) {
    triggerKeyLED();
  }
}

// =================================================================================
// SETUP & WEBSERVER HANDLERS
// =================================================================================
void setup() {
  pinMode(LED_PIN, OUTPUT);
  setBoardLED(false);

  // Khoi tao UART truyen sang MCU khac (Baud 115200)
  Serial1.begin(115200, SERIAL_8N1, S3_RX_PIN, S3_TX_PIN);
  delay(500);

#if ENABLE_WIFI
  // Khoi tao WiFi Access Point (AP Mode)
  WiFi.softAP("ESP32S3-USB-Bridge");
  WiFi.setTxPower(WIFI_POWER_2dBm); // Cong suat phat gan thap nhat (2 dBm) giup chip chay cuc mat
  
  // Khai bao cac duong dan Web Server
  server.on("/", []() {
    server.send_P(200, "text/html", INDEX_HTML);
  });

  server.on("/get-logs", []() {
    unsigned long clientSeq = 0;
    if (server.hasArg("last")) {
      clientSeq = server.arg("last").toInt();
    }

    String json = "{\"seq\":" + String(webLogSequence) + 
                  ",\"typed\":\"" + String(typedTextBuffer) + "\"";
    
    // Gửi danh sách các phím đang được nhấn
    json += ",\"keys\":[";
    for (int k = 0; k < activeKeyCount; k++) {
      json += String(activeKeyCodes[k]);
      if (k < activeKeyCount - 1) json += ",";
    }
    json += "],\"logs\":[";
    
    if (clientSeq < webLogSequence) {
      unsigned long missed = webLogSequence - clientSeq;
      if (missed > MAX_WEB_LOGS) missed = MAX_WEB_LOGS;

      int startIdx = (webLogHead - missed + MAX_WEB_LOGS) % MAX_WEB_LOGS;
      for (unsigned long i = 0; i < missed; i++) {
        int idx = (startIdx + i) % MAX_WEB_LOGS;
        String esc = webLogs[idx];
        esc.replace("\"", "\\\"");
        esc.replace("\n", "\\n");
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

  // Nháy đèn LED đỏ đơn 3 lần lúc khởi động
  for (int i = 0; i < 3; i++) {
    setBoardLED(true);
    delay(120);
    setBoardLED(false);
    delay(120);
  }

  // Dang ky USB HID Input callback cho Ban phim
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

  // TỰ ĐỘNG TẮT ĐÈN LED ĐỎ SAU 120MS KHI DỪNG THAO TÁC GÕ PHÍM
  if (ledActive && (now - lastKeyActionTime >= 120)) {
    ledActive = false;
    digitalWrite(LED_PIN, LOW);
  }

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

  // 2. In log su kien & gui len Web Ring Buffer
  if (logMessageAvailable) {
    logMessageAvailable = false;
    Serial1.print(logMessageBuffer);
#if ENABLE_WIFI
    addWebLog(String(logMessageBuffer));
#endif
  }

  // 3. Gui dinh ky moi 20ms sang MCU khac
  if (now - lastSendTime >= 20) {
    lastSendTime = now;
    sendDataToUart();
  }

  delay(2);
}
