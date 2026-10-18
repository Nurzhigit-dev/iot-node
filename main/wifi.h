#pragma once
#include "esp_err.h"

/* Connect to the Wi-Fi network from app_config.h. Blocks until an IP address is received. */
esp_err_t wifi_connect(void);