/**
 * File: gconfig.h
 * Author: Diego Parrilla Santamaría
 * Date: November 2024, February 2026
 * Copyright: 2024-2026 - GOODDATA LABS SL
 * Description: Header file for the global configuration manager
 */

#ifndef GCONFIG_H
#define GCONFIG_H

#include "constants.h"
#include "debug.h"
#include "settings.h"

#define PARAM_APPS_FOLDER "APPS_FOLDER"
#define PARAM_APPS_CATALOG_URL "APPS_CATALOG_URL"
#define PARAM_BOOT_FEATURE "BOOT_FEATURE"
#define PARAM_HOSTNAME "HOSTNAME"
#define PARAM_SAFE_CONFIG_REBOOT "SAFE_CONFIG_REBOOT"
#define PARAM_SD_BAUD_RATE_KB "SD_BAUD_RATE_KB"
#define PARAM_WIFI_AUTH "WIFI_AUTH"
#define PARAM_WIFI_CONNECT_TIMEOUT "WIFI_CONNECT_TIMEOUT"
#define PARAM_WIFI_COUNTRY "WIFI_COUNTRY"
#define PARAM_WIFI_DHCP "WIFI_DHCP"
#define PARAM_WIFI_DNS "WIFI_DNS"
#define PARAM_WIFI_IP "WIFI_IP"
#define PARAM_WIFI_MODE "WIFI_MODE"
#define PARAM_WIFI_NETMASK "WIFI_NETMASK"
#define PARAM_WIFI_GATEWAY "WIFI_GATEWAY"
#define PARAM_WIFI_PASSWORD "WIFI_PASSWORD"
#define PARAM_WIFI_POWER "WIFI_POWER"
#define PARAM_WIFI_RSSI "WIFI_RSSI"
#define PARAM_WIFI_SCAN_SECONDS "WIFI_SCAN_SECONDS"
#define PARAM_WIFI_SSID "WIFI_SSID"

#define GCONFIG_SUCCESS 0
#define GCONFIG_INIT_ERROR -1
#define GCONFIG_MISMATCHED_APP -2

int gconfig_init(const char *current_app_name);
SettingsContext *gconfig_getContext(void);

#endif  // GCONFIG_H
