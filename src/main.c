#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <stdbool.h>

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
#include "nvs_flash.h"

#define TOPIC_MAX_LEN 128

static const char *TAG = "garage";

typedef enum {
    GARAGE_STATE_LISTENING = 0,
    GARAGE_STATE_TRIGGERING,
    GARAGE_STATE_THROTTLED,
} garage_state_t;

typedef enum {
    CONTROL_CMD_OPEN = 0,
    CONTROL_CMD_THROTTLE_EXPIRED,
    CONTROL_CMD_PUBLISH_HEARTBEAT,
    CONTROL_CMD_PUBLISH_STATE_SNAPSHOT,
} control_cmd_t;

typedef struct {
    control_cmd_t cmd;
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

static void control_task(void *param);
static void wifi_init_sta(void);
static void mqtt_start(void);
static void publish_state_message(const char *type, bool retain, const char *extra_key, int32_t extra_value);
static int32_t remaining_cooldown_ms(void);
static bool mqtt_is_connected(void);
static void update_status_led(void);
static bool control_post(control_cmd_t cmd);
static void handle_open_request(void);
static void handle_throttle_expired(void);
static void handle_publish_heartbeat(void);
static void handle_publish_snapshot(void);
static void process_command_payload(const char *data, int len);

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
    cJSON_AddStringToObject(root, "deviceId", CONFIG_GARAGE_DEVICE_ID);
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
    if (s_last_trigger_us == 0) {
        return 0;
    }
    int64_t elapsed_us = esp_timer_get_time() - s_last_trigger_us;
    if (elapsed_us < 0) {
        return CONFIG_GARAGE_DEBOUNCE_MS;
    }
    int32_t remaining = CONFIG_GARAGE_DEBOUNCE_MS - (int32_t)(elapsed_us / 1000);
    return remaining > 0 ? remaining : 0;
}

static void update_status_led(void)
{
#if CONFIG_GARAGE_STATUS_LED_GPIO >= 0
    int level = (s_state == GARAGE_STATE_LISTENING) ? 0 : 1;
    esp_err_t err = gpio_set_level(CONFIG_GARAGE_STATUS_LED_GPIO, level);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to update status LED: %s", esp_err_to_name(err));
    }
#endif
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
    publish_state_message("state", true, "durationMs", CONFIG_GARAGE_RELAY_PULSE_MS);

    esp_err_t err = gpio_set_level(CONFIG_GARAGE_RELAY_GPIO, s_relay_active_level);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to activate relay: %s", esp_err_to_name(err));
    }
    vTaskDelay(pdMS_TO_TICKS(CONFIG_GARAGE_RELAY_PULSE_MS));
    err = gpio_set_level(CONFIG_GARAGE_RELAY_GPIO, s_relay_inactive_level);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to deactivate relay: %s", esp_err_to_name(err));
    }

    s_last_trigger_us = esp_timer_get_time();
    s_state = GARAGE_STATE_THROTTLED;
    update_status_led();
    publish_state_message("state", true, "cooldownMs", CONFIG_GARAGE_DEBOUNCE_MS);

    if (s_debounce_timer) {
        xTimerStop(s_debounce_timer, 0);
        xTimerChangePeriod(s_debounce_timer, pdMS_TO_TICKS(CONFIG_GARAGE_DEBOUNCE_MS), 0);
        xTimerStart(s_debounce_timer, 0);
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
    strncpy((char *)wifi_config.sta.ssid, CONFIG_GARAGE_WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strncpy((char *)wifi_config.sta.password, CONFIG_GARAGE_WIFI_PASSWORD, sizeof(wifi_config.sta.password));
    if (strlen(CONFIG_GARAGE_WIFI_PASSWORD) == 0) {
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
        .credentials.client_id = CONFIG_GARAGE_DEVICE_ID,
        .credentials.username = CONFIG_GARAGE_MQTT_USERNAME,
        .credentials.authentication.password = CONFIG_GARAGE_MQTT_PASSWORD,
        .session.keepalive = CONFIG_GARAGE_HEARTBEAT_INTERVAL_S > 0 ? CONFIG_GARAGE_HEARTBEAT_INTERVAL_S : 60,
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

    s_connection_event_group = xEventGroupCreate();
    ensure(s_connection_event_group != NULL, "Failed to create connection event group");

    s_control_queue = xQueueCreate(10, sizeof(control_message_t));
    ensure(s_control_queue != NULL, "Failed to create control queue");

    if (CONFIG_GARAGE_RELAY_ACTIVE_HIGH) {
        s_relay_active_level = 1;
        s_relay_inactive_level = 0;
    } else {
        s_relay_active_level = 0;
        s_relay_inactive_level = 1;
    }

    gpio_config_t relay_conf = {
        .pin_bit_mask = 1ULL << CONFIG_GARAGE_RELAY_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&relay_conf));
    ESP_ERROR_CHECK(gpio_set_level(CONFIG_GARAGE_RELAY_GPIO, s_relay_inactive_level));

#if CONFIG_GARAGE_STATUS_LED_GPIO >= 0
    gpio_config_t led_conf = {
        .pin_bit_mask = 1ULL << CONFIG_GARAGE_STATUS_LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&led_conf));
    ESP_ERROR_CHECK(gpio_set_level(CONFIG_GARAGE_STATUS_LED_GPIO, 0));
#endif

    if (CONFIG_GARAGE_DEBOUNCE_MS > 0) {
        s_debounce_timer = xTimerCreate("debounce", pdMS_TO_TICKS(CONFIG_GARAGE_DEBOUNCE_MS), pdFALSE, NULL, debounce_timer_callback);
        ensure(s_debounce_timer != NULL, "Failed to create debounce timer");
    }

    if (CONFIG_GARAGE_HEARTBEAT_INTERVAL_S > 0) {
        s_heartbeat_timer = xTimerCreate(
            "heartbeat",
            pdMS_TO_TICKS(CONFIG_GARAGE_HEARTBEAT_INTERVAL_S * 1000),
            pdTRUE,
            NULL,
            heartbeat_timer_callback);
        ensure(s_heartbeat_timer != NULL, "Failed to create heartbeat timer");
        xTimerStart(s_heartbeat_timer, 0);
    }

    BaseType_t task_created = xTaskCreate(control_task, "control_task", 4096, NULL, 5, NULL);
    ensure(task_created == pdPASS, "Failed to create control task");

    snprintf(s_command_topic, sizeof(s_command_topic), "garage/%s/command", CONFIG_GARAGE_DEVICE_ID);
    snprintf(s_state_topic, sizeof(s_state_topic), "garage/%s/state", CONFIG_GARAGE_DEVICE_ID);
    snprintf(s_mqtt_uri, sizeof(s_mqtt_uri), "mqtts://%s:%d", CONFIG_GARAGE_MQTT_HOST, CONFIG_GARAGE_MQTT_PORT);

    wifi_init_sta();
    mqtt_start();

    control_post(CONTROL_CMD_PUBLISH_STATE_SNAPSHOT);
}
