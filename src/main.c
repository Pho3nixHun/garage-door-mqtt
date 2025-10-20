#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <stdbool.h>
#include <ctype.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/timers.h"

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "driver/gpio.h"
#include "esp_event.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "mqtt_client.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "nvs.h"
#include "nvs_flash.h"

#define TOPIC_MAX_LEN 128
#define OTA_TAG_MAX_LEN 64
#define OTA_ASSET_MAX_LEN 96

static const char *TAG = "garage";

typedef enum {
    GARAGE_STATE_LISTENING = 0,
    GARAGE_STATE_TRIGGERING,
    GARAGE_STATE_THROTTLED,
    GARAGE_STATE_UPDATING,
} garage_state_t;

typedef enum {
    CONTROL_CMD_OPEN = 0,
    CONTROL_CMD_THROTTLE_EXPIRED,
    CONTROL_CMD_PUBLISH_HEARTBEAT,
    CONTROL_CMD_PUBLISH_STATE_SNAPSHOT,
    CONTROL_CMD_START_OTA,
} control_cmd_t;

typedef struct {
    control_cmd_t cmd;
    char ota_tag[OTA_TAG_MAX_LEN];
    char ota_asset[OTA_ASSET_MAX_LEN];
} control_message_t;

static EventGroupHandle_t s_connection_event_group;
static QueueHandle_t s_control_queue;
static TimerHandle_t s_debounce_timer;
static TimerHandle_t s_heartbeat_timer;
static esp_mqtt_client_handle_t s_mqtt_client;

#define WIFI_CONNECTED_BIT BIT0
#define MQTT_CONNECTED_BIT BIT1

static garage_state_t s_state = GARAGE_STATE_LISTENING;
static int64_t s_last_trigger_us = 0;
static int s_relay_active_level = 1;
static int s_relay_inactive_level = 0;

static char s_command_topic[TOPIC_MAX_LEN];
static char s_state_topic[TOPIC_MAX_LEN];
static char s_mqtt_uri[TOPIC_MAX_LEN];

#define GARAGE_CONFIG_NAMESPACE "garage"
#define GARAGE_CONFIG_KEY "config"

typedef struct {
    char *wifi_ssid;
    char *wifi_password;
    char *device_id;
    char *mqtt_host;
    int mqtt_port;
    char *mqtt_username;
    char *mqtt_password;
    int relay_gpio;
    int status_led_gpio;
    bool relay_active_high;
    int relay_pulse_ms;
    int debounce_ms;
    int heartbeat_interval_s;
    char *ota_repo_owner;
    char *ota_repo_name;
} garage_config_t;

static garage_config_t s_config = {
    .status_led_gpio = -1,
};

static void control_task(void *param);
static void garage_config_load(void);
static void ensure(bool condition, const char *message);
static void wifi_init_sta(void);
static void mqtt_start(void);
static void publish_state_message(const char *type, bool retain, const char *extra_key, int32_t extra_value);
static int32_t remaining_cooldown_ms(void);
static bool mqtt_is_connected(void);
static void update_status_led(void);
static bool control_post(control_cmd_t cmd);
static bool control_post_ota(const char *tag, const char *asset);
static void handle_start_ota(const char *tag, const char *asset);
static void handle_open_request(void);
static void handle_throttle_expired(void);
static void handle_publish_heartbeat(void);
static void handle_publish_snapshot(void);
static void process_command_payload(const char *data, int len);
static void publish_ota_status(const char *status, const char *detail, esp_err_t err);
static bool is_valid_release_component(const char *value, size_t max_len);

static char *duplicate_json_string_field(const cJSON *root, const char *field)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, field);
    char msg[96];
    snprintf(msg, sizeof(msg), "Missing or invalid string field: %s", field);
    ensure(cJSON_IsString(item) && item->valuestring, msg);

    char *copy = strdup(item->valuestring);
    ensure(copy != NULL, "Out of memory duplicating config string");
    return copy;
}

static int extract_json_int_field(const cJSON *root, const char *field)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, field);
    char msg[96];
    snprintf(msg, sizeof(msg), "Missing or invalid integer field: %s", field);
    ensure(cJSON_IsNumber(item), msg);
    return item->valueint;
}

static bool extract_json_bool_field(const cJSON *root, const char *field)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, field);
    char msg[96];
    snprintf(msg, sizeof(msg), "Missing boolean field: %s", field);
    ensure(item != NULL, msg);

    if (cJSON_IsBool(item)) {
        return cJSON_IsTrue(item);
    }
    if (cJSON_IsNumber(item)) {
        return item->valueint != 0;
    }
    if (cJSON_IsString(item) && item->valuestring) {
        char c = (char)tolower((unsigned char)item->valuestring[0]);
        if (c == 'y' || c == 't' || c == '1') {
            return true;
        }
        if (c == 'n' || c == 'f' || c == '0') {
            return false;
        }
    }

    snprintf(msg, sizeof(msg), "Invalid boolean field: %s", field);
    ensure(false, msg);
    return false;
}

static void garage_config_load(void)
{
    if (s_config.wifi_ssid) {
        return;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(GARAGE_CONFIG_NAMESPACE, NVS_READONLY, &handle);
    ensure(err == ESP_OK, "Failed to open NVS namespace 'garage'");

    size_t json_size = 0;
    err = nvs_get_str(handle, GARAGE_CONFIG_KEY, NULL, &json_size);
    ensure(err == ESP_OK && json_size > 0, "Missing garage config JSON in NVS");

    char *json = malloc(json_size);
    ensure(json != NULL, "Out of memory allocating config buffer");

    err = nvs_get_str(handle, GARAGE_CONFIG_KEY, json, &json_size);
    nvs_close(handle);
    ensure(err == ESP_OK, "Failed to read garage config JSON from NVS");

    cJSON *root = cJSON_Parse(json);
    free(json);
    ensure(root != NULL, "Failed to parse garage config JSON");

    s_config.wifi_ssid = duplicate_json_string_field(root, "CONFIG_GARAGE_WIFI_SSID");
    s_config.wifi_password = duplicate_json_string_field(root, "CONFIG_GARAGE_WIFI_PASSWORD");
    s_config.device_id = duplicate_json_string_field(root, "CONFIG_GARAGE_DEVICE_ID");
    s_config.mqtt_host = duplicate_json_string_field(root, "CONFIG_GARAGE_MQTT_HOST");
    s_config.mqtt_port = extract_json_int_field(root, "CONFIG_GARAGE_MQTT_PORT");
    s_config.mqtt_username = duplicate_json_string_field(root, "CONFIG_GARAGE_MQTT_USERNAME");
    s_config.mqtt_password = duplicate_json_string_field(root, "CONFIG_GARAGE_MQTT_PASSWORD");
    s_config.relay_gpio = extract_json_int_field(root, "CONFIG_GARAGE_RELAY_GPIO");
    s_config.status_led_gpio = extract_json_int_field(root, "CONFIG_GARAGE_STATUS_LED_GPIO");
    s_config.relay_active_high = extract_json_bool_field(root, "CONFIG_GARAGE_RELAY_ACTIVE_HIGH");
    s_config.relay_pulse_ms = extract_json_int_field(root, "CONFIG_GARAGE_RELAY_PULSE_MS");
    s_config.debounce_ms = extract_json_int_field(root, "CONFIG_GARAGE_DEBOUNCE_MS");
    s_config.heartbeat_interval_s = extract_json_int_field(root, "CONFIG_GARAGE_HEARTBEAT_INTERVAL_S");
    s_config.ota_repo_owner = duplicate_json_string_field(root, "CONFIG_GARAGE_OTA_REPO_OWNER");
    s_config.ota_repo_name = duplicate_json_string_field(root, "CONFIG_GARAGE_OTA_REPO_NAME");

    cJSON_Delete(root);

    ESP_LOGI(TAG, "Loaded garage config for device '%s'", s_config.device_id);
}

static void ensure(bool condition, const char *message)
{
    if (!condition) {
        ESP_LOGE(TAG, "%s", message);
        abort();
    }
}

static const char *state_to_string(garage_state_t state)
{
    switch (state) {
        case GARAGE_STATE_LISTENING:
            return "LISTENING";
        case GARAGE_STATE_TRIGGERING:
            return "TRIGGERING";
        case GARAGE_STATE_THROTTLED:
            return "THROTTLED";
        case GARAGE_STATE_UPDATING:
            return "UPDATING";
        default:
            return "UNKNOWN";
    }
}

static bool mqtt_is_connected(void)
{
    if (!s_mqtt_client) {
        return false;
    }
    EventBits_t bits = xEventGroupGetBits(s_connection_event_group);
    return (bits & MQTT_CONNECTED_BIT) != 0;
}

static void publish_ota_status(const char *status, const char *detail, esp_err_t err)
{
    if (!mqtt_is_connected()) {
        ESP_LOGI(TAG, "Skipping OTA status publish (%s); MQTT not connected", status);
        return;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        ESP_LOGE(TAG, "Failed to allocate JSON for OTA status");
        return;
    }

    cJSON_AddStringToObject(root, "type", "ota");
    cJSON_AddStringToObject(root, "status", status);
    cJSON_AddStringToObject(root, "deviceId", s_config.device_id);
    cJSON_AddNumberToObject(root, "timestamp", esp_timer_get_time() / 1000);

    if (detail) {
        cJSON_AddStringToObject(root, "detail", detail);
    }
    if (err != ESP_OK) {
        cJSON_AddStringToObject(root, "error", esp_err_to_name(err));
    }

    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!payload) {
        ESP_LOGE(TAG, "Failed to serialize OTA status");
        return;
    }

    int msg_id = esp_mqtt_client_publish(s_mqtt_client, s_state_topic, payload, 0, 1, 0);
    if (msg_id < 0) {
        ESP_LOGW(TAG, "Failed to publish OTA status: %s", status);
    } else {
        ESP_LOGI(TAG, "Published OTA status '%s' (msg_id=%d)", status, msg_id);
    }

    cJSON_free(payload);
}

static void publish_state_message(const char *type, bool retain, const char *extra_key, int32_t extra_value)
{
    if (!mqtt_is_connected()) {
        ESP_LOGD(TAG, "Skipping %s publish; MQTT not connected", type);
        return;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        ESP_LOGE(TAG, "Failed to allocate JSON root");
        return;
    }

    cJSON_AddStringToObject(root, "type", type);
    cJSON_AddStringToObject(root, "state", state_to_string(s_state));
    cJSON_AddStringToObject(root, "deviceId", s_config.device_id);
    cJSON_AddNumberToObject(root, "timestamp", esp_timer_get_time() / 1000);

    if (extra_key) {
        cJSON_AddNumberToObject(root, extra_key, extra_value);
    }

    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!payload) {
        ESP_LOGE(TAG, "Failed to serialize JSON");
        return;
    }

    int msg_id = esp_mqtt_client_publish(s_mqtt_client, s_state_topic, payload, 0, 1, retain ? 1 : 0);
    if (msg_id < 0) {
        ESP_LOGW(TAG, "Failed to publish %s message", type);
    } else {
        ESP_LOGI(TAG, "Published %s message id=%d", type, msg_id);
    }

    cJSON_free(payload);
}

static int32_t remaining_cooldown_ms(void)
{
    if (s_last_trigger_us == 0 || s_config.debounce_ms <= 0) {
        return 0;
    }
    int64_t elapsed_us = esp_timer_get_time() - s_last_trigger_us;
    if (elapsed_us < 0) {
        return s_config.debounce_ms;
    }
    int32_t remaining = s_config.debounce_ms - (int32_t)(elapsed_us / 1000);
    return remaining > 0 ? remaining : 0;
}

static bool is_valid_release_component(const char *value, size_t max_len)
{
    if (!value) {
        return false;
    }
    size_t len = strnlen(value, max_len + 1);
    if (len == 0 || len > max_len) {
        return false;
    }
    if (strstr(value, "..") != NULL) {
        return false;
    }
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)value[i];
        if (!(isalnum(c) || c == '-' || c == '_' || c == '.')) {
            return false;
        }
    }
    return true;
}

static void update_status_led(void)
{
    if (s_config.status_led_gpio < 0) {
        return;
    }
    int level = (s_state == GARAGE_STATE_LISTENING) ? 0 : 1;
    esp_err_t err = gpio_set_level(s_config.status_led_gpio, level);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to update status LED: %s", esp_err_to_name(err));
    }
}

static void handle_throttle_expired(void)
{
    if (s_state != GARAGE_STATE_THROTTLED) {
        return;
    }
    s_state = GARAGE_STATE_LISTENING;
    update_status_led();
    publish_state_message("state", true, NULL, 0);
}

static void handle_publish_heartbeat(void)
{
    const char *extra_key = NULL;
    int32_t extra_value = 0;
    if (s_state == GARAGE_STATE_THROTTLED) {
        extra_key = "cooldownMs";
        extra_value = remaining_cooldown_ms();
    }
    publish_state_message("heartbeat", false, extra_key, extra_value);
}

static void handle_publish_snapshot(void)
{
    const char *extra_key = NULL;
    int32_t extra_value = 0;
    if (s_state == GARAGE_STATE_THROTTLED) {
        extra_key = "cooldownMs";
        extra_value = remaining_cooldown_ms();
    }
    publish_state_message("state", true, extra_key, extra_value);
}

static void handle_open_request(void)
{
    if (s_state == GARAGE_STATE_UPDATING) {
        ESP_LOGW(TAG, "Ignoring open command during OTA update");
        publish_state_message("state", true, NULL, 0);
        return;
    }

    int32_t remaining = remaining_cooldown_ms();
    if (s_state == GARAGE_STATE_TRIGGERING) {
        ESP_LOGW(TAG, "Relay already triggering; ignoring duplicate open command");
        publish_state_message("state", true, NULL, 0);
        return;
    }

    if (remaining > 0) {
        ESP_LOGI(TAG, "Debounce active (%d ms remaining)", remaining);
        publish_state_message("state", true, "cooldownMs", remaining);
        return;
    }

    s_state = GARAGE_STATE_TRIGGERING;
    update_status_led();
    publish_state_message("state", true, "durationMs", s_config.relay_pulse_ms);

    esp_err_t err = gpio_set_level(s_config.relay_gpio, s_relay_active_level);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to activate relay: %s", esp_err_to_name(err));
    }
    vTaskDelay(pdMS_TO_TICKS(s_config.relay_pulse_ms));
    err = gpio_set_level(s_config.relay_gpio, s_relay_inactive_level);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to deactivate relay: %s", esp_err_to_name(err));
    }

    s_last_trigger_us = esp_timer_get_time();
    s_state = GARAGE_STATE_THROTTLED;
    update_status_led();
    publish_state_message("state", true, "cooldownMs", s_config.debounce_ms);

    if (s_debounce_timer && s_config.debounce_ms > 0) {
        xTimerStop(s_debounce_timer, 0);
        xTimerChangePeriod(s_debounce_timer, pdMS_TO_TICKS(s_config.debounce_ms), 0);
        xTimerStart(s_debounce_timer, 0);
    }
}

static void handle_start_ota(const char *tag, const char *asset)
{
    if (s_state == GARAGE_STATE_UPDATING) {
        ESP_LOGW(TAG, "OTA already in progress");
        publish_ota_status("rejected", "update-in-progress", ESP_ERR_INVALID_STATE);
        return;
    }

    if (!is_valid_release_component(tag, OTA_TAG_MAX_LEN - 1) ||
        !is_valid_release_component(asset, OTA_ASSET_MAX_LEN - 1)) {
        ESP_LOGW(TAG, "Invalid OTA tag or asset");
        publish_ota_status("rejected", "invalid-tag-or-asset", ESP_ERR_INVALID_ARG);
        return;
    }

    char path_detail[160];
    int detail_written = snprintf(path_detail, sizeof(path_detail), "%s/%s", tag, asset);
    if (detail_written < 0 || detail_written >= (int)sizeof(path_detail)) {
        ESP_LOGE(TAG, "OTA detail string too long");
        publish_ota_status("failure", "detail-too-long", ESP_ERR_INVALID_SIZE);
        return;
    }

    char url[256];
    int written = snprintf(
        url,
        sizeof(url),
        "https://github.com/%s/%s/releases/download/%s/%s",
        s_config.ota_repo_owner,
        s_config.ota_repo_name,
        tag,
        asset);
    if (written < 0 || written >= (int)sizeof(url)) {
        ESP_LOGE(TAG, "OTA URL too long");
        publish_ota_status("failure", "url-too-long", ESP_ERR_INVALID_SIZE);
        return;
    }

    ESP_LOGI(TAG, "Starting OTA update from %s", url);

    s_state = GARAGE_STATE_UPDATING;
    update_status_led();
    publish_state_message("state", true, NULL, 0);
    publish_ota_status("started", path_detail, ESP_OK);

    esp_http_client_config_t http_cfg = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10000,
    };
    esp_https_ota_config_t ota_cfg = {
        .http_config = &http_cfg,
    };

    esp_err_t err = esp_https_ota(&ota_cfg);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "OTA update succeeded; restarting");
        publish_ota_status("success", path_detail, ESP_OK);
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    } else {
        ESP_LOGE(TAG, "OTA update failed: %s", esp_err_to_name(err));
        publish_ota_status("failure", path_detail, err);
        s_state = GARAGE_STATE_LISTENING;
        update_status_led();
        publish_state_message("state", true, NULL, 0);
    }
}

static bool control_post(control_cmd_t cmd)
{
    if (!s_control_queue) {
        return false;
    }
    control_message_t msg = {
        .cmd = cmd,
    };
    BaseType_t queued = xQueueSend(s_control_queue, &msg, 0);
    if (queued != pdTRUE) {
        ESP_LOGW(TAG, "Control queue full; dropping cmd %d", (int)cmd);
        return false;
    }
    return true;
}

static bool control_post_ota(const char *tag, const char *asset)
{
    if (!s_control_queue) {
        return false;
    }
    control_message_t msg = { 0 };
    msg.cmd = CONTROL_CMD_START_OTA;
    snprintf(msg.ota_tag, sizeof(msg.ota_tag), "%s", tag);
    snprintf(msg.ota_asset, sizeof(msg.ota_asset), "%s", asset);

    BaseType_t queued = xQueueSend(s_control_queue, &msg, 0);
    if (queued != pdTRUE) {
        ESP_LOGW(TAG, "Control queue full; dropping OTA request");
        return false;
    }
    return true;
}

static void control_task(void *param)
{
    control_message_t message;
    while (xQueueReceive(s_control_queue, &message, portMAX_DELAY) == pdTRUE) {
        switch (message.cmd) {
            case CONTROL_CMD_OPEN:
                handle_open_request();
                break;
            case CONTROL_CMD_THROTTLE_EXPIRED:
                handle_throttle_expired();
                break;
            case CONTROL_CMD_PUBLISH_HEARTBEAT:
                handle_publish_heartbeat();
                break;
            case CONTROL_CMD_PUBLISH_STATE_SNAPSHOT:
                handle_publish_snapshot();
                break;
            case CONTROL_CMD_START_OTA:
                handle_start_ota(message.ota_tag, message.ota_asset);
                break;
            default:
                ESP_LOGW(TAG, "Unhandled control command %d", (int)message.cmd);
                break;
        }
    }
    vTaskDelete(NULL);
}

static void debounce_timer_callback(TimerHandle_t timer)
{
    control_post(CONTROL_CMD_THROTTLE_EXPIRED);
}

static void heartbeat_timer_callback(TimerHandle_t timer)
{
    control_post(CONTROL_CMD_PUBLISH_HEARTBEAT);
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "Wi-Fi started, connecting...");
                esp_wifi_connect();
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGW(TAG, "Wi-Fi disconnected, retrying...");
                xEventGroupClearBits(s_connection_event_group, WIFI_CONNECTED_BIT);
                esp_wifi_connect();
                break;
            default:
                break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_connection_event_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init_sta(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    ensure(sta_netif != NULL, "Failed to create default Wi-Fi STA");

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = { 0 };
    strncpy((char *)wifi_config.sta.ssid, s_config.wifi_ssid, sizeof(wifi_config.sta.ssid) - 1);
    wifi_config.sta.ssid[sizeof(wifi_config.sta.ssid) - 1] = '\0';
    strncpy((char *)wifi_config.sta.password, s_config.wifi_password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.password[sizeof(wifi_config.sta.password) - 1] = '\0';

    if (strlen(s_config.wifi_password) == 0) {
        wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    } else {
        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    switch (event_id) {
        case MQTT_EVENT_CONNECTED: {
            ESP_LOGI(TAG, "MQTT connected");
            xEventGroupSetBits(s_connection_event_group, MQTT_CONNECTED_BIT);
            int msg_id = esp_mqtt_client_subscribe(event->client, s_command_topic, 1);
            if (msg_id < 0) {
                ESP_LOGE(TAG, "Failed to subscribe to %s", s_command_topic);
            } else {
                ESP_LOGI(TAG, "Subscribed to %s (msg_id=%d)", s_command_topic, msg_id);
            }
            control_post(CONTROL_CMD_PUBLISH_STATE_SNAPSHOT);
            break;
        }
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT disconnected");
            xEventGroupClearBits(s_connection_event_group, MQTT_CONNECTED_BIT);
            break;
        case MQTT_EVENT_DATA:
            if (event->topic && event->topic_len == strlen(s_command_topic) &&
                strncmp(event->topic, s_command_topic, event->topic_len) == 0) {
                process_command_payload(event->data, event->data_len);
            } else {
                ESP_LOGW(TAG, "Unhandled MQTT data on topic %.*s", event->topic_len, event->topic);
            }
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT event error encountered");
            break;
        default:
            break;
    }
}

static void process_command_payload(const char *data, int len)
{
    char *buffer = malloc((size_t)len + 1);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate buffer for command payload");
        return;
    }
    memcpy(buffer, data, len);
    buffer[len] = '\0';

    cJSON *root = cJSON_Parse(buffer);
    free(buffer);

    if (!root) {
        ESP_LOGW(TAG, "Invalid JSON command payload");
        return;
    }

    const cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (cJSON_IsString(type) && type->valuestring) {
        if (strcmp(type->valuestring, "open") == 0) {
            ESP_LOGI(TAG, "Received open command via MQTT");
            control_post(CONTROL_CMD_OPEN);
        } else if (strcmp(type->valuestring, "ota") == 0) {
            const cJSON *tag = cJSON_GetObjectItemCaseSensitive(root, "tag");
            const cJSON *asset = cJSON_GetObjectItemCaseSensitive(root, "asset");
            if (!cJSON_IsString(tag) || !tag->valuestring || !cJSON_IsString(asset) || !asset->valuestring) {
                ESP_LOGW(TAG, "OTA command missing tag or asset");
                publish_ota_status("rejected", "missing-tag-or-asset", ESP_ERR_INVALID_ARG);
            } else if (!is_valid_release_component(tag->valuestring, OTA_TAG_MAX_LEN - 1) ||
                       !is_valid_release_component(asset->valuestring, OTA_ASSET_MAX_LEN - 1)) {
                ESP_LOGW(TAG, "OTA command has invalid characters");
                publish_ota_status("rejected", "invalid-tag-or-asset", ESP_ERR_INVALID_ARG);
            } else if (!control_post_ota(tag->valuestring, asset->valuestring)) {
                publish_ota_status("rejected", "queue-full", ESP_ERR_NO_MEM);
            } else {
                ESP_LOGI(TAG, "Received OTA command for %s/%s", tag->valuestring, asset->valuestring);
            }
        } else {
            ESP_LOGW(TAG, "Unknown command type: %s", type->valuestring);
        }
    } else {
        ESP_LOGW(TAG, "Command missing type field");
    }

    cJSON_Delete(root);
}

static void mqtt_start(void)
{
    if (s_mqtt_client) {
        ESP_LOGI(TAG, "Restarting MQTT client");
        ESP_ERROR_CHECK(esp_mqtt_client_reconnect(s_mqtt_client));
        return;
    }

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = s_mqtt_uri,
        .broker.verification.crt_bundle_attach = esp_crt_bundle_attach,
        .credentials.client_id = s_config.device_id,
        .credentials.username = s_config.mqtt_username,
        .credentials.authentication.password = s_config.mqtt_password,
        .session.keepalive = s_config.heartbeat_interval_s > 0 ? s_config.heartbeat_interval_s : 60,
    };

    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    ensure(s_mqtt_client != NULL, "Failed to create MQTT client");

    ESP_ERROR_CHECK(esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL));
    ESP_ERROR_CHECK(esp_mqtt_client_start(s_mqtt_client));
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    garage_config_load();

    s_connection_event_group = xEventGroupCreate();
    ensure(s_connection_event_group != NULL, "Failed to create connection event group");

    s_control_queue = xQueueCreate(10, sizeof(control_message_t));
    ensure(s_control_queue != NULL, "Failed to create control queue");

    if (s_config.relay_active_high) {
        s_relay_active_level = 1;
        s_relay_inactive_level = 0;
    } else {
        s_relay_active_level = 0;
        s_relay_inactive_level = 1;
    }

    gpio_config_t relay_conf = {
        .pin_bit_mask = 1ULL << s_config.relay_gpio,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&relay_conf));
    ESP_ERROR_CHECK(gpio_set_level(s_config.relay_gpio, s_relay_inactive_level));

    if (s_config.status_led_gpio >= 0) {
        gpio_config_t led_conf = {
            .pin_bit_mask = 1ULL << s_config.status_led_gpio,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&led_conf));
        ESP_ERROR_CHECK(gpio_set_level(s_config.status_led_gpio, 0));
    }

    if (s_config.debounce_ms > 0) {
        s_debounce_timer = xTimerCreate("debounce", pdMS_TO_TICKS(s_config.debounce_ms), pdFALSE, NULL, debounce_timer_callback);
        ensure(s_debounce_timer != NULL, "Failed to create debounce timer");
    }

    if (s_config.heartbeat_interval_s > 0) {
        s_heartbeat_timer = xTimerCreate(
            "heartbeat",
            pdMS_TO_TICKS(s_config.heartbeat_interval_s * 1000),
            pdTRUE,
            NULL,
            heartbeat_timer_callback);
        ensure(s_heartbeat_timer != NULL, "Failed to create heartbeat timer");
        xTimerStart(s_heartbeat_timer, 0);
    }

    BaseType_t task_created = xTaskCreate(control_task, "control_task", 4096, NULL, 5, NULL);
    ensure(task_created == pdPASS, "Failed to create control task");

    snprintf(s_command_topic, sizeof(s_command_topic), "garage/%s/command", s_config.device_id);
    snprintf(s_state_topic, sizeof(s_state_topic), "garage/%s/state", s_config.device_id);
    snprintf(s_mqtt_uri, sizeof(s_mqtt_uri), "mqtts://%s:%d", s_config.mqtt_host, s_config.mqtt_port);

    wifi_init_sta();
    mqtt_start();

    control_post(CONTROL_CMD_PUBLISH_STATE_SNAPSHOT);
}
