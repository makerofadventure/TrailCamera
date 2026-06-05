# TrailCamera

A power-efficient, motion-activated trail camera built with Arduino. This project is designed to sit quietly in the wild, waking up only to capture images when wildlife or movement is detected, and saving those images directly to an SD card.

## How It Works
The camera operates in two distinct modes to make deployment and operation as seamless as possible:

### 1. Setup Mode (Wireless Access Point)
Upon first starting up, the camera broadcasts its own Wi-Fi network, acting as an Access Point. By connecting your phone or laptop to this network, you can access a web interface that provides:
* **Live View:** Stream a live feed from the camera to help you perfectly frame and position your shot in the wild.
* **Gallery & Download:** View and download previously captured wildlife photos directly to your device without removing the SD card.
* **Clear Storage:** Easily wipe the SD card to prepare for a brand-new deployment.
* **Arm Camera:** A button to exit Setup Mode, sync the camera's internal Real-Time Clock (RTC) to your browser's time for accurate photo file timestamps, and put the camera into its ultra-low-power deep sleep mode to begin monitoring.

> **Note:** Take a screenshot of the Setup Mode web interface and save it as `UI_Screenshot.jpg` in this directory to display it here.

!Web Interface UI

### 2. Trail Mode (Deep Sleep)
Once the device is physically deployed and armed via the web interface:
1. **Standby:** The system shuts down its Wi-Fi and enters an ultra-low-power deep sleep mode.
2. **Motion Detection:** A PIR sensor continuously monitors the area for heat/movement signatures.
3. **Wake Up:** Upon detecting motion, the PIR sensor triggers a hardware interrupt, immediately waking the microcontroller from deep sleep.
4. **Capture & Store:** The camera module powers up, captures an image, and writes the file to the SD card.
5. **Sleep:** The system automatically returns to deep sleep to wait for the next trigger.

## Why Deep Sleep?
Trail cameras are typically deployed outdoors and rely completely on battery power for weeks or months at a time. If the processor and camera were running continuously, the batteries would die in a matter of hours. By utilizing **deep sleep**, the microcontroller shuts down non-essential internal clocks and peripherals (like the camera and SD card reader), dropping its power draw to mere microamps. It only consumes significant power for the brief moments it is awake to take a picture, massively extending the battery life of the device.

## Power-On Behavior & Setup Entry
When the camera is cold-booted (power switch turned ON):
1. **Initial Boot Latency (7–10 seconds of silence):** Upon powering on, **the camera will appear completely dead for 7 to 10 seconds** (no LED activity). This is because the PIR sensor requires time to stabilize, during which it holds its output HIGH and pulls **GPIO 12** (the ESP32's flash voltage strapping pin) HIGH. This forces the ESP32 into a silent temporary boot loop. Once the PIR warms up and goes LOW, the ESP32 boots normally. This 7-10 second silent pause is completely normal.
2. **Setup Mode Entry Window (1 second of lit LED):** Immediately after the 7-10 second silent pause, the Flash LED will turn on dimly for **exactly 1 second**. 
   * **To enter Setup Mode (Wi-Fi AP):** You must turn the power switch **OFF while this LED is active (lit)**, and then turn it back ON. On the next boot, the camera will enter Setup Mode and broadcast `TrailCam_Setup`.
   * **To stay in Monitoring Mode:** Do nothing. After the 1-second window, the LED turns off, the camera waits for the PIR sensor to settle, and then enters low-power deep sleep.

## Blink Codes (White Flash LED)
The camera uses the built-in white Flash LED (configured at a very dim brightness to protect your eyes) to indicate its status:
* **1 blink:** Heartbeat logged (hourly timer wake, appends to `heartbeat.txt`).
* **2 blinks:** Photo successfully captured and saved to the SD card.
* **3 blinks:** Motion detected (PIR wakeup trigger).
* **4 blinks:** SD Card mount/save failure.
* **5 blinks:** Webserver setup mode starting (SSID `TrailCam_Setup`).
* **10 fast blinks:** Camera initialization error.
* **1-second solid dim pulse:** Device is entering deep sleep mode.

## Wiring & Pinouts
Wire up the components as detailed below, then compile and upload the software using the Arduino IDE. 

| Component | Pin | Description |
| :--- | :--- | :--- |
| **Power System** | `5V` / `GND`| Powered by 6V (4x AA batteries) connected to the board |
| **PIR Sensor VCC** | `3.3V` | Powered from the ESP32 3.3V pin |
| **PIR Sensor GND** | `GND` | Ground connection |
| **PIR Sensor Out** | `Pin 12` | Hardware interrupt to wake the board from deep sleep |
| **SD Card & Camera**| `Built-in`| Using the ESP32-CAM's onboard micro SD slot (1-bit mode) and camera |

![Circuit Layout](Trailcam_bb.jpg)