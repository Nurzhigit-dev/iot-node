#pragma once

/* Settings for a real board (USE_SIMULATOR 0 in app_config.h).
   Copy this file to secrets.h in the same folder and fill it in.
   secrets.h is git-ignored, so your Wi-Fi password never ends up on GitHub. */

/* Your Wi-Fi network. The ESP32-S3 only sees 2.4 GHz networks. */
#define WIFI_SSID      "your-wifi-name"
#define WIFI_PASSWORD  "your-wifi-password"

/* IPv4 address of the PC that runs the Docker server stack, on the same network.
   Find it with `ipconfig` ("IPv4 Address" of your Wi-Fi or Ethernet adapter). */
#define SERVER_HOST    "192.168.1.100"
