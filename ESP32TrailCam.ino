#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>
#include "SD_MMC.h"
#include <time.h>
#include "soc/soc.h"           // Needed for WRITE_PERI_REG
#include "soc/rtc_cntl_reg.h"  // Needed for RTC_CNTL_BROWN_OUT_REG
#include <esp_wifi.h>
// --- (Keep your existing Pin Definitions and Camera Config here) ---
#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27
#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22
#define PIR_SENSOR_PIN GPIO_NUM_13


#define RED_LED_PIN 33

RTC_DATA_ATTR bool isMonitoring = false;
WebServer server(80);
IPAddress local_ip(10, 11, 12, 13);
IPAddress gateway(10, 11, 12, 13);
IPAddress subnet(255, 255, 255, 0);



// --- Expanded HTML Page ---
const char* htmlPage = R"rawliteral(
<!DOCTYPE html><html><head><meta name="viewport" content="width=device-width, initial-scale=1">
<style>
  body{text-align:center; font-family:sans-serif; background:#f4f4f4; padding:20px;}
  .card{background:white; padding:20px; border-radius:10px; box-shadow:0 2px 10px rgba(0,0,0,0.1); margin-bottom:20px;}
  img{max-width:100%; height:auto; border-radius:5px;}
  .btn{display:block; width:100%; padding:15px; margin:10px 0; border:none; border-radius:5px; font-size:16px; cursor:pointer;}
  .btn-start{background:#27ae60; color:white;}
  .btn-dl{background:#2980b9; color:white;}
  .btn-del{background:#c0392b; color:white;}
</style>
</head><body>
  <div class="card">
    <h2>Live Aim</h2>
    <img src="/capture" id="photo">
    <button class="btn btn-start" onclick="startMonitoring()">START MONITORING</button>
  </div>

  <div class="card">
    <h2>Storage Management</h2>
    <button class="btn btn-dl" onclick="downloadAll()">DOWNLOAD ALL PHOTOS</button>
    <button class="btn btn-del" onclick="confirmDelete()">DELETE ALL PHOTOS</button>
  </div>

<script>
  setInterval(function(){ document.getElementById('photo').src='/capture?v=' + Date.now(); }, 3000);
  
  function startMonitoring() {
    const now = Math.floor(Date.now() / 1000);
    location.href = '/start?t=' + now;
  }

  async function downloadAll() {
    const response = await fetch('/listfiles');
    const files = await response.json();
    if(files.length === 0) { alert("No photos found"); return; }
    
    for (const file of files) {
      const link = document.createElement('a');
      link.href = '/download?file=' + file;
      link.download = file;
      document.body.appendChild(link);
      link.click();
      document.body.removeChild(link);
      await new Promise(r => setTimeout(r, 500)); // Small delay to not crash browser/ESP
    }
  }

  function confirmDelete() {
    if(confirm("Are you sure you want to delete ALL photos from the SD card?")) {
      location.href = '/deleteall';
    }
  }
</script>
</body></html>)rawliteral";

// --- Helper Functions ---

void saveToSD(camera_fb_t* fb) {
  // 1. Initialize SD Card in 1-bit mode
  if (!SD_MMC.begin("/sdcard", true)) {
    Serial.println("SD Card Mount Failed");
    return;
  }

  // 2. Get the current time from the internal RTC
  time_t now;
  struct tm timeinfo;
  time(&now);
  localtime_r(&now, &timeinfo);

  // 3. Create the filename string (e.g., /2026-04-29_10-15-30.jpg)
  char fileName[50];
  strftime(fileName, sizeof(fileName), "/%Y-%m-%d_%H-%M-%S.jpg", &timeinfo);

  // 4. Save the image
  File file = SD_MMC.open(fileName, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open file for writing");
  } else {
    file.write(fb->buf, fb->len);
    Serial.printf("Saved photo: %s\n", fileName);
  }

  // 5. Cleanup
  file.close();
  SD_MMC.end();  // Unmount to ensure data is flushed and saved
}

void handleListFiles() {
  if (!SD_MMC.begin("/sdcard", true)) {
    server.send(500, "text/plain", "SD Fail");
    return;
  }
  File root = SD_MMC.open("/");
  String output = "[";
  File file = root.openNextFile();
  while (file) {
    if (String(file.name()).endsWith(".jpg")) {
      if (output != "[") output += ",";
      output += "\"" + String(file.name()) + "\"";
    }
    file = root.openNextFile();
  }
  output += "]";
  server.send(200, "application/json", output);
  SD_MMC.end();
}

void handleDownload() {
  String path = server.arg("file");
  if (!path.startsWith("/")) path = "/" + path;
  if (!SD_MMC.begin("/sdcard", true)) return;
  File file = SD_MMC.open(path, FILE_READ);
  if (!file) {
    server.send(404, "text/plain", "Not Found");
    return;
  }
  server.streamFile(file, "image/jpeg");
  file.close();
  SD_MMC.end();
}

void handleDeleteAll() {
  if (!SD_MMC.begin("/sdcard", true)) return;
  File root = SD_MMC.open("/");
  File file = root.openNextFile();
  while (file) {
    String filename = String(file.name());
    file.close();  // Close before deleting
    SD_MMC.remove("/" + filename);
    file = root.openNextFile();
  }
  SD_MMC.end();
  server.send(200, "text/html", "All files deleted. <a href='/'>Back</a>");
}


void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);  // Disable brownout detector
  Serial.begin(115200);
  pinMode(RED_LED_PIN, OUTPUT);

  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0 || wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) {
    isMonitoring = true;
    Serial.println("Wake to take photo");
  } else {
    Serial.println("Wake to Wifi Setup");

    isMonitoring = false;
  }
  // 2.  Camera Configuration
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_VGA;
  config.jpeg_quality = 10;
  config.fb_count = 1;
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  sensor_t* s = esp_camera_sensor_get();
  s->set_framesize(s, FRAMESIZE_VGA);

  if (!isMonitoring) {
    WiFi.softAPConfig(local_ip, gateway, subnet);
    WiFi.softAP("TrailCam_Setup", "2efdaf4b59");
    esp_wifi_set_ps(WIFI_PS_NONE);
    // Routes
    server.on("/", HTTP_GET, []() {
      server.send(200, "text/html", htmlPage);
    });
    server.on("/capture", HTTP_GET, []() {
      camera_fb_t* fb = esp_camera_fb_get();

      if (!fb) {
        Serial.println("No FB");
        server.send(500, "text/plain", "Capture Failed");
        return;
      }
      server.send_P(200, "image/jpeg", (const char*)fb->buf, fb->len);
      esp_camera_fb_return(fb);
    });
    server.on("/listfiles", HTTP_GET, handleListFiles);
    server.on("/download", HTTP_GET, handleDownload);
    server.on("/deleteall", HTTP_GET, handleDeleteAll);
    server.on("/start", HTTP_GET, []() {
      if (server.hasArg("t")) {
        uint32_t timestamp = server.arg("t").toInt();

        // 2. Update the ESP32 internal RTC
        struct timeval tv;
        tv.tv_sec = timestamp;
        tv.tv_usec = 0;
        settimeofday(&tv, NULL);

        Serial.printf("Time synced to: %u\n", timestamp);
      }
      isMonitoring = true;
      server.send(200, "text/plain", "Monitoring starting...");
      delay(1000);
      sleepNow();
    });

    server.begin();
    Serial.println("WiFi Server Started");
  } else {
 
    s->set_whitebal(s, 1);       // Enable AWB
    s->set_wb_mode(s, 0);        // 0: Auto, 1: Sunny, 2: Cloudy...
    s->set_exposure_ctrl(s, 1);  // Enable AEC

    camera_fb_t* fb;
    //10 lowres shots to get AWB right
    for (int x = 0; x < 4; x++) {
      fb = esp_camera_fb_get();
      if (fb){
        esp_camera_fb_return(fb);}
    }

    fb = esp_camera_fb_get();
    if (fb) {
      saveToSD(fb);
      esp_camera_fb_return(fb);
    } else {
      Serial.println("Error - no Image in fb");
    }
 
    delay(5000);  // Wait a bit between shots
    sleepNow();
  }
}


void sleepNow() {
  // Prepare for Sleep
  // Using 1-bit SD mode frees up GPIO 13 for the PIR
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PIR_SENSOR_PIN, 1);

  //Uncomment for timed shots for testing
  //uint64_t sleepTime = 5 * 1000000;  // 5 seconds
  //esp_sleep_enable_timer_wakeup(sleepTime);

  Serial.println("Entering Deep Sleep");
  esp_deep_sleep_start();
}

int lastCount = 0;

void loop() {
  if (!isMonitoring) {
    server.handleClient();

    // Check how many devices are connected to the AP
    int currentCount = WiFi.softAPgetStationNum();

    if (currentCount > lastCount) {
      Serial.printf(">>> Client Connected! Total: %d\n", currentCount);
      lastCount = currentCount;
    } else if (currentCount < lastCount) {
      Serial.printf("<<< Client Disconnected. Total: %d\n", currentCount);
      lastCount = currentCount;
    }
  }
}