# VBOT — autonomous ESP32 desk buddy

Open **[`vbot/vbot.ino`](vbot/vbot.ino)** in **Arduino IDE** and upload it to your ESP32. It is a **single, self-contained `.ino` file**. You can also copy its entire contents into a new Arduino IDE sketch and save it as `vbot`; no local header or `.cpp` files are needed. No server, website, AI subscription, microphone, buttons, or sensors are needed.

VBOT animates a face, shows spontaneous coded messages and break suggestions, takes short pixel naps, and occasionally displays tech headlines when internet is available. Offline messages are programmed personality, not AI conversation. It cannot hear you or detect your presence.

The original `robobot.ino` remains a separate, unchanged sketch. Do not move `vbot.ino` next to it: Arduino combines `.ino` files in the same folder.

## Hardware

This version targets a **classic ESP32-WROOM / ESP32 Dev Module with 4 MB flash**, using the original project's **128×64 SH1106 I²C OLED**. Other ESP32 variants require pin changes and validation; this sketch deliberately rejects them during compilation.

| OLED | ESP32 |
| --- | --- |
| VCC | 3V3 |
| GND | GND |
| SDA | GPIO21 |
| SCL | GPIO22 |

The display address is `0x3C`. `U8G2_R2` rotates it 180°. Change that to `U8G2_R0` if necessary. For an unreliable I²C bus, change `setBusClock(400000)` to `setBusClock(100000)`.

## Upload from Arduino IDE

1. In Boards Manager, install **esp32 by Espressif Systems, version 3.3.12**. This version uses the `network_provisioning` API; older cores are not compatible without changes.
2. In Library Manager, install **U8g2 by oliver**. Networking, provisioning, and Preferences come with the ESP32 board package.
3. Open **`vbot/vbot.ino`**, not the original `robobot.ino`.
4. Select **Tools → Board → esp32 → ESP32 Dev Module** and your USB port.
5. Set **Flash Size → 4MB** and **Partition Scheme → Huge APP (3MB No OTA/1MB SPIFFS)**. BLE, Wi-Fi, and certificate verification need more space than the default application partition.
6. Keep **Core Debug Level → None**; verbose ESP32 library diagnostics can print provisioning credentials.
7. Click **Upload**. If your board does not enter upload mode automatically, hold its BOOT button while the IDE is connecting, then release it. No button is needed during normal operation.

The Serial Monitor at **115200 baud** provides optional diagnostics. Daily use needs only power.

If an old copied sketch reports **`vbot_network.h: No such file or directory`**, replace its entire contents with the current `vbot/vbot.ino`, or open that file directly. The current version is standalone and does not include that header. Merely deleting the include from the old version will not fix it.

## First-time Android setup

1. Install Espressif's **ESP Provisioning / ESP BLE Provisioning** Android app from [Espressif's apps page](https://www.espressif.com/en/support/download/apps) or its [official Android project](https://github.com/espressif/esp-idf-provisioning-android).
2. Enable Bluetooth and grant the app its requested nearby-device permissions (older Android versions may also require location permission for scanning).
3. Power on VBOT. A setup card appears periodically, showing **`PROV_VBOT_XXXXXX`** and an **eight-digit PIN**.
4. In the app, choose BLE provisioning and the manual/no-QR device-selection option. Select the displayed device and enter its PIN, including leading zeroes. Connect inside the app, not Android's Bluetooth pairing settings.
5. Select your **2.4 GHz Wi-Fi** and enter its password. For a phone hotspot, enable the hotspot in 2.4 GHz/compatibility mode and supply its name and password. If using that same phone hides its hotspot from the scan list, use manual network entry where available or provision it from another phone.
6. Once connected, the setup card disappears. VBOT saves the last working network and reconnects on future boots.

The PIN is generated on the device and saved. Provisioning uses Espressif Security 1 with proof of possession. Wi-Fi passwords are not printed by VBOT. Credentials remain in ordinary ESP32 flash; this hobby sketch does not enable flash encryption.

Bluetooth is a **setup channel**, not a permanent phone connection or remote-control app. It closes after successful setup. No custom setup link is included in this version.

## Changing Wi-Fi without any buttons

If the saved network is unavailable for approximately **90 seconds**, VBOT automatically advertises Bluetooth setup again and shows its name/PIN. Use the same Android app to supply a replacement network. To switch while the old network is still available, temporarily turn that old router/hotspot off and wait for setup to appear.

VBOT retains the last known working credentials while setup is pending, and retries them about every 30 seconds when no new credentials are being tested. A wrong password leaves setup available for another attempt after a short delay. Only **one** last working network is stored, not a list of home and hotspot profiles. A successful replacement overwrites the previous one.

The phone's hotspot must be enabled on the phone; VBOT cannot turn it on. Networks requiring a browser sign-in/captive portal or enterprise authentication are not supported.

## Autonomous behavior

- Face animations run at approximately 30 FPS, independently of network requests.
- Coded chatter appears roughly every 18–40 seconds while awake.
- After about four minutes awake, VBOT takes a 25-second dimmed pixel nap and wakes itself. This is display dimming, not deep sleep.
- It attempts to fetch the **Ars Technica Technology Lab RSS feed** after connecting, then every 30 minutes. Failed requests retry after five minutes. A Wi-Fi connection alone does not guarantee internet/news access.
- About every three minutes while awake, it selects an unseen headline when one is available. It shows the source and word-wrapped pages for seven seconds each.
- Up to eight headlines are held in RAM. The last 32 displayed headline hashes prevent recent repeats; both cache and history reset on reboot. Titles longer than 240 display characters are explicitly shortened with `...`.
- Cached headlines are labelled when offline, after a failed refresh, or when the last successful fetch is over 35 minutes old. They stop being shown after six hours without a successful fetch. This is **fetch age**, not article publication age.
- No internet? Expressions and coded chatter continue. Network operations run in a separate task and use bounded buffers and timeouts.

The small display uses an ASCII font. Common smart quotes and dashes are simplified; unsupported characters become spaces. This version does not include interactive chat, audio, phone controls, or the original RoboBot's manually started focus/breathing modes.

## Screen status

| Label | Meaning |
| --- | --- |
| `OFFLINE` | Not connected to Wi-Fi; local personality continues |
| `SETUP` | Android Bluetooth provisioning is available |
| `WI-FI` | Connected to Wi-Fi; news not yet fetched or the last attempt failed |
| `NEWS OK` | Last feed request succeeded |
| `NET ERROR` | Network worker or Bluetooth setup could not start; inspect Serial Monitor |

HTTPS uses the ESP32 core's certificate bundle, and the clock synchronizes with NTP before fetching. Certificate validation is never disabled. If NTP, DNS, or the feed is blocked, VBOT stays functional offline and retries. Feed redirects or unexpected formats fail safely; a future publisher URL change may require updating `FEED_URL` in `vbot.ino`.

## Customization and verification

Edit the phrases and `NEWS_INTERVAL_MS`, `AWAKE_MS`, `REST_MS`, `PAGE_MS`, and `BRIGHTNESS` in `vbot.ino`. Feed URL and refresh timing live in `vbot.ino`; the reader supports RSS 2.0 item titles, not arbitrary news APIs or Atom feeds.

Host-side parser checks (optional, not needed for Arduino upload):

```sh
python3 tests/test_news.py
```

Before leaving the device running unattended, check first-time pairing, a wrong-password retry, power-cycle reconnection, hotspot loss/return, screen readability, and an extended run on the actual board. Compilation and parser tests cannot verify radio or OLED behavior.

Build verification: compiled successfully with Arduino ESP32 **3.3.12**, **ESP32 Dev Module**, **Huge APP**: approximately 1.9 MB of flash (60%) and 71 KB of static RAM (21%). Host parser tests passed with address/undefined-behavior sanitizers (leak detection disabled because the test sandbox does not support it). Physical hardware and live feed retrieval have not been tested here.

References: [Espressif provisioning](https://github.com/espressif/arduino-esp32/tree/master/libraries/WiFiProv/examples/WiFiProv), [Ars Technica RSS feeds](https://arstechnica.com/rss-feeds/). Face-animation heritage: [Tech Talkies](https://www.youtube.com/@techtalkies1).
