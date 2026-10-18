#pragma once

const char *ota_running_version(void);
void ota_confirm_running_app(void);
void ota_start(const char *url, int len);