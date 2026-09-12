/*
 * =================================================================================
 * DERICHS 2026 - ESP32-S3 SUPERMINI: UNIVERSAL USB MOUSE RECEIVER (TEMPLATE)
 * =================================================================================
 * 
 * HO CONG NGHE - HARDWARE CONNECTIONS:
 * 1. UART Simplex (Truyen tin hieu sang Vi dieu khien khac):
 *    - TX Pin: GPIO 4 (TX1 cua ESP32-S3) ---> Nối vào chân RX của MCU nhận
 *    - GND Pin: GND cua ESP32-S3          ---> Nối chung GND với MCU nhận
 * 2. Status LED Indicator:
 *    - LED Pin: GPIO 48 (LED đỏ đơn cạnh cổng Type-C - Sáng khi thao tác chuột, tự tắt sau 120ms)
 * 3. Unused Pins:
 *    - RX Pin: GPIO 5 (Gán làm chân ảo để khởi tạo UART safely, không nối dây)
 * 
 * 4. WiFi AP Config (Doc Serial & Mouse Dashboard qua Web):
 *    - SSID: ESP32S3-USB-Bridge
 *    - IP Address: 192.168.4.1 (Truy cập qua trình duyệt Web trên điện thoại/máy tính)
 * 
 * ---------------------------------------------------------------------------------
 * HUONG DAN DANH CHO LAP TRINH VIEN VA AI (DEVELOPER & AI INSTRUCTIONS):
 * Code nay la khung nguon mau (Template) chuyen dung de doc CHUOT USB qua cong Type-C.
 * Giao dien Web giu nguyen Terminal Log cu, va co nut bam de mo rong giao dien
 * truc quan hinh Con Chuot + Man Hinh monitor.
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
// Dat 1 de BAT WiFi Web Mouse Dashboard, dat 0 de TAT (Giup mach chay muot ma hon)
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
// HAM DIEU KHIEN LED DO DON CANH CONG TYPE-C (PIN 48) - TOI UU SIEU SHEED
// =================================================================================
inline void setBoardLED(bool state) {
  digitalWrite(LED_PIN, state ? HIGH : LOW);
}

// Bien quan ly thoi gian tu dong tat den LED khi dung thao tac
volatile unsigned long lastMouseActionTime = 0;
volatile bool ledActive = false;

inline void triggerMouseLED() {
  lastMouseActionTime = millis();
  if (!ledActive) {
    ledActive = true;
    digitalWrite(LED_PIN, HIGH);
  }
}

// =================================================================================
// PHAN A: BIEN TRANG THAI CHUOT (MOUSE STATE VARIABLES)
// =================================================================================
EspUsbHost usbHost;

#if ENABLE_WIFI
WebServer server(80);
#endif

// Bien trang thai Chuột USB (Mouse States)
volatile int mouseX = 200;       // Vi tri X tren man hinh ao (0..400)
volatile int mouseY = 125;       // Vi tri Y tren man hinh ao (0..250)
volatile int lastDx = 0;         // Do lech X cuoi cung
volatile int lastDy = 0;         // Do lech Y cuoi cung
volatile bool mouseBtnL = false; // Nut chuot Trai (Left Click)
volatile bool mouseBtnR = false; // Nut chuot Phai (Right Click)
volatile bool mouseBtnM = false; // Nut chuot Giua (Middle Click / Wheel)
volatile int8_t mouseWheel = 0;  // Con cuon chuot (Scroll Wheel)
volatile uint32_t webWheelPulse = 0; // Xung dem lan lan chuot cho Web JS

// Bien dong bo truyen du lieu UART & Log
unsigned long lastSendTime = 0;
unsigned long lastLogThrottleTime = 0;

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

// HTML & GIAO DIEN WEB (TERMINAL LOG CU + NUT MO RONG HINH CON CHUOT & MAN HINH TRUC QUAN)
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP32-S3 USB Mouse Dashboard</title>
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
    .visual-container { display:none; background:#0f172a; border:1px solid #1e293b; border-radius:8px; padding:12px; margin-bottom:10px; gap:12px; }
    .visual-container.open { display:flex; flex-wrap:wrap; }
    
    /* MO PHONG HINH CON CHUOT SVG */
    .mouse-card { flex:1; min-width:180px; background:#020617; border:1px solid #1e293b; border-radius:8px; padding:12px; display:flex; flex-direction:column; align-items:center; justify-content:center; }
    .svg-mouse { width:110px; height:160px; }
    .mouse-part { transition: fill 0.1s, filter 0.1s; }
    
    /* MAU SAC NHAY SANG KHI NHAN NUT / LAN CHUOT */
    .active-l { fill: #22c55e !important; filter: drop-shadow(0 0 12px #22c55e); }
    .active-r { fill: #ef4444 !important; filter: drop-shadow(0 0 12px #ef4444); }
    .active-w { fill: #f59e0b !important; filter: drop-shadow(0 0 12px #f59e0b); }

    /* MO PHONG MAN HINH MONITOR VIRTUAL */
    .screen-card { flex:2; min-width:260px; background:#020617; border:1px solid #1e293b; border-radius:8px; padding:10px; display:flex; flex-direction:column; }
    .coord-info { font-size:12px; color:#38bdf8; font-weight:bold; margin-bottom:6px; text-align:right; }
    .screen-box { width:100%; height:140px; background:#030712; border:1px dashed #334155; border-radius:6px; position:relative; overflow:hidden; }
    .grid-pattern { width:100%; height:100%; background-image: radial-gradient(#1e293b 1px, transparent 1px); background-size: 14px 14px; }
    
    .cursor-ptr { position:absolute; width:16px; height:16px; pointer-events:none; z-index:10; }
    .cursor-ptr svg { width:100%; height:100%; fill:#38bdf8; filter: drop-shadow(0 2px 4px rgba(0,0,0,0.9)); }
    
    /* TERMINAL LOG MAN HINH CU (MAC DINH HIENTHI MAIN) */
    .terminal { flex-grow:1; background:#030712; border:1px solid #1f2937; border-radius:8px; padding:12px; overflow-y:auto; white-space:pre-wrap; font-size:12px; }
    .line { margin:3px 0; line-height:1.4; border-bottom:1px solid #111827; padding-bottom:2px; }
    .mouse-log { color:#f43f5e; }
    .debug-log { color:#9ca3af; }
  </style>
</head>
<body>
  <div class="header">
    <span class="title">ESP32-S3 USB MOUSE RECEIVER</span>
    <span class="status">AP: ESP32S3-USB-Bridge (192.168.4.1)</span>
  </div>

  <!-- NUT BAM BAT/TAT GIAO DIEN TRUC QUAN -->
  <div class="toggle-btn" id="btn-toggle" onclick="toggleVisual()">
    [ MO GIAO DIEN HINH CHUOT & MAN HINH TRUC QUAN ]
  </div>

  <!-- KHUNG TRUC QUAN (HINH CHUOT + MAN HINH) -->
  <div class="visual-container" id="visual-panel">
    <!-- HINH CON CHUOT BEN TRAI -->
    <div class="mouse-card">
      <svg viewBox="0 0 120 180" class="svg-mouse">
        <!-- Than chuot -->
        <path d="M 20 60 C 20 20, 100 20, 100 60 L 100 120 C 100 160, 20 160, 20 120 Z" fill="#1e293b" stroke="#334155" stroke-width="3"/>
        <!-- Duong chia giua -->
        <line x1="60" y1="20" x2="60" y2="70" stroke="#334155" stroke-width="2"/>
        <!-- Nut Trai -->
        <path id="svg-btn-l" d="M 22 55 C 22 25, 58 25, 58 55 L 58 70 L 22 70 Z" fill="#334155" class="mouse-part"/>
        <!-- Nut Phai -->
        <path id="svg-btn-r" d="M 62 55 C 62 25, 98 25, 98 55 L 98 70 L 62 70 Z" fill="#334155" class="mouse-part"/>
        <!-- Con cuon chuot -->
        <rect id="svg-wheel" x="53" y="40" width="14" height="24" rx="7" fill="#64748b" class="mouse-part"/>
      </svg>
    </div>

    <!-- MAN HINH AO BEN PHAI -->
    <div class="screen-card">
      <div class="coord-info" id="coords">X: 200 | Y: 125 | ΔX: +0 | ΔY: +0 | WHEEL: 0</div>
      <div class="screen-box" id="screen">
        <div class="grid-pattern"></div>
        <div class="cursor-ptr" id="cursor" style="left:50%; top:50%;">
          <svg viewBox="0 0 24 24"><path d="M3 3l7 18 3-7 7-3L3 3z"/></svg>
        </div>
      </div>
    </div>
  </div>

  <!-- TERMINAL LOG CUL MAN HINH MAC DINH -->
  <div class="terminal" id="term">Connecting to ESP32-S3...</div>

  <script>
    let lastSeq = 0;
    let lastWp = 0;
    let wheelTimer = null;
    const term = document.getElementById('term');
    const cursor = document.getElementById('cursor');
    const coords = document.getElementById('coords');
    const screen = document.getElementById('screen');
    const svgL = document.getElementById('svg-btn-l');
    const svgR = document.getElementById('svg-btn-r');
    const svgW = document.getElementById('svg-wheel');
    const visualPanel = document.getElementById('visual-panel');
    const btnToggle = document.getElementById('btn-toggle');

    function toggleVisual() {
      if (visualPanel.classList.contains('open')) {
        visualPanel.classList.remove('open');
        btnToggle.textContent = '[ MO GIAO DIEN HINH CHUOT & MAN HINH TRUC QUAN ]';
      } else {
        visualPanel.classList.add('open');
        btnToggle.textContent = '[ DONG GIAO DIEN TRUC QUAN ]';
      }
    }

    function fetchLogs() {
      fetch('/get-logs?last=' + lastSeq)
        .then(r => r.json())
        .then(data => {
          if (data.mx !== undefined) {
            const screenW = screen.clientWidth;
            const screenH = screen.clientHeight;
            const posX = (data.mx / 400) * (screenW - 16);
            const posY = (data.my / 250) * (screenH - 16);
            
            cursor.style.left = posX + 'px';
            cursor.style.top = posY + 'px';
            
            coords.textContent = `X: ${data.mx} | Y: ${data.my} | ΔX: ${data.dx >= 0 ? '+' : ''}${data.dx} | ΔY: ${data.dy >= 0 ? '+' : ''}${data.dy} | WHEEL: ${data.wheel}`;
            
            // Nháy sáng nút trái, nút phải khi nhấn
            svgL.className = 'mouse-part ' + (data.btnL ? 'active-l' : '');
            svgR.className = 'mouse-part ' + (data.btnR ? 'active-r' : '');
            
            // Nháy sáng con cuộn khi lăn chuột hoặc nhấn cuộn
            if (data.btnM || (data.wp !== undefined && data.wp !== lastWp)) {
              lastWp = data.wp;
              svgW.className = 'mouse-part active-w';
              clearTimeout(wheelTimer);
              wheelTimer = setTimeout(() => {
                svgW.className = 'mouse-part';
              }, 400);
            }
          }

          // Cập nhật Terminal Log cũ
          if(data.logs && data.logs.length > 0) {
            if(lastSeq === 0) term.innerHTML = '';
            data.logs.forEach(l => {
              const div = document.createElement('div');
              div.className = l.includes('[MOUSE]') ? 'line mouse-log' : 'line debug-log';
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
  uint8_t mouseBtnByte = 0;
  if (mouseBtnL) mouseBtnByte |= (1 << 0);
  if (mouseBtnR) mouseBtnByte |= (1 << 1);
  if (mouseBtnM) mouseBtnByte |= (1 << 2);

  Serial1.printf("$MOUSE,%d,%d,%d,%d,%d,%d\n", 
                 mouseX, mouseY, lastDx, lastDy, mouseBtnByte, mouseWheel);
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
                  ",\"mx\":" + String(mouseX) + 
                  ",\"my\":" + String(mouseY) + 
                  ",\"dx\":" + String(lastDx) + 
                  ",\"dy\":" + String(lastDy) + 
                  ",\"btnL\":" + String(mouseBtnL ? 1 : 0) + 
                  ",\"btnR\":" + String(mouseBtnR ? 1 : 0) + 
                  ",\"btnM\":" + String(mouseBtnM ? 1 : 0) + 
                  ",\"wheel\":" + String(mouseWheel) + 
                  ",\"wp\":" + String(webWheelPulse) + 
                  ",\"logs\":[";
    
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

  // Nháy đèn LED đỏ đơn 3 lần lúc khởi động
  for (int i = 0; i < 3; i++) {
    setBoardLED(true);
    delay(120);
    setBoardLED(false);
    delay(120);
  }

  // Dang ky USB Host Native Mouse Callback (Tự động nhận diện mọi loại Chuột USB)
  usbHost.onMouse([](const EspUsbHostMouseEvent &event) {
    int16_t dx = event.x;
    int16_t dy = event.y;
    int16_t wheel = event.wheel;
    uint8_t buttons = event.buttons;

    bool btnL = (buttons & 0x01) != 0;
    bool btnR = (buttons & 0x02) != 0;
    bool btnM = (buttons & 0x04) != 0;

    mouseBtnL = btnL;
    mouseBtnR = btnR;
    mouseBtnM = btnM;
    mouseWheel = wheel;
    lastDx = dx;
    lastDy = dy;

    if (wheel != 0) {
      webWheelPulse++;
    }

    // Cập nhật vị trí con trỏ trong màn hình ảo 400x250
    mouseX += dx;
    mouseY += dy;

    if (mouseX < 0) mouseX = 0;
    if (mouseX > 400) mouseX = 400;
    if (mouseY < 0) mouseY = 0;
    if (mouseY > 250) mouseY = 250;

    // Bật đèn LED đỏ đơn chân 48 khi có bất kỳ thao tác chuột nào (Tối ưu nhẹ)
    bool hasAction = (dx != 0 || dy != 0 || wheel != 0 || btnL || btnR || btnM);
    if (hasAction) {
      triggerMouseLED();

      // Giới hạn tần suất tạo log văn bản (Throttle 100ms) để giữ cho Web chạy mượt mà 100%
      if (millis() - lastLogThrottleTime > 100 || btnL || btnR || btnM || wheel != 0) {
        lastLogThrottleTime = millis();
        snprintf(logMessageBuffer, sizeof(logMessageBuffer), 
                 "[MOUSE] Pos:(%d,%d) | Delta:(%+d,%+d) | Buttons:(L:%d R:%d M:%d) | Wheel:%d\n", 
                 mouseX, mouseY, dx, dy, btnL ? 1 : 0, btnR ? 1 : 0, btnM ? 1 : 0, wheel);
        logMessageAvailable = true;
      }
    }
  });

  // Dang ky USB HID Input callback cho Log Debug
  usbHost.onHIDInput([](const EspUsbHostHIDInput &input) {
    if (input.length == 0 || input.data == nullptr) return;

    if (input.length <= 64) {
      debugIface = input.interfaceNumber;
      debugLen = input.length;
      memcpy(debugBuffer, input.data, input.length);
      debugDataAvailable = true;
    }
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

  // TỰ ĐỘNG TẮT ĐÈN LED ĐỎ SAU 120MS KHI DỪNG THAO TÁC CHUỘT (SIÊU NHẸ)
  if (ledActive && (now - lastMouseActionTime >= 120)) {
    ledActive = false;
    digitalWrite(LED_PIN, LOW); // Tắt trực tiếp LED
  }

  // 1. In debug HID tho & gui len Web Ring Buffer
  if (debugDataAvailable) {
    debugDataAvailable = false;
    char tmp[128];
    int len = snprintf(tmp, sizeof(tmp), "Mouse Iface %d | len=%d | Data: ", debugIface, debugLen);
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
