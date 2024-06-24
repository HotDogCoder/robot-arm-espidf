#include <stdio.h>
#include <string.h>
#include <driver/ledc.h>
#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_http_server.h>
#include <cJSON.h>
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_mac.h"
#include <ctype.h> // For isspace function

// WiFi credentials
#define WIFI_SSID "robot_arm"
#define WIFI_PASS "cM4M6X$AA*HIeezk" // Ensure password is at least 8 characters long

#define SERVO_PIN_BASE GPIO_NUM_27
#define SERVO_PIN_SHOULDER GPIO_NUM_26
#define SERVO_PIN_ELBOW GPIO_NUM_25
#define SERVO_PIN_GRIPPER GPIO_NUM_33

#define SERVO_MIN_PULSEWIDTH_US 500
#define SERVO_MAX_PULSEWIDTH_US 2500
#define SERVO_MAX_DEGREE 180

#define MIN(a, b) (((a) < (b)) ? (a) : (b))

static const char *TAG = "robot_arm";

// Initialize servos
void init_servo(gpio_num_t gpio_num, ledc_channel_t channel)
{
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_16_BIT,
        .freq_hz = 50,
        .clk_cfg = LEDC_AUTO_CLK};
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_channel = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = channel,
        .timer_sel = LEDC_TIMER_0,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = gpio_num,
        .duty = 0,
        .hpoint = 0};
    ledc_channel_config(&ledc_channel);
}

// Set servo angle
void set_servo_angle(ledc_channel_t channel, uint32_t angle)
{
    uint32_t duty = SERVO_MIN_PULSEWIDTH_US + (((SERVO_MAX_PULSEWIDTH_US - SERVO_MIN_PULSEWIDTH_US) * angle) / SERVO_MAX_DEGREE);
    duty = (duty * 8191) / 20000;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, channel, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, channel);
}

// WiFi event handler
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_START)
    {
        ESP_LOGI(TAG, "WiFi AP started");
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STOP)
    {
        ESP_LOGI(TAG, "WiFi AP stopped");
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED)
    {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "Client connected: MAC=" MACSTR, MAC2STR(event->mac));
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED)
    {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "Client disconnected: MAC=" MACSTR, MAC2STR(event->mac));
    }
}

// Initialize WiFi in AP mode
void wifi_init_ap(void)
{
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_register(WIFI_EVENT,
                                        ESP_EVENT_ANY_ID,
                                        &wifi_event_handler,
                                        NULL,
                                        &instance_any_id);

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = WIFI_SSID,
            .ssid_len = strlen(WIFI_SSID),
            .password = WIFI_PASS,
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA2_PSK}};

    if (strlen(WIFI_PASS) == 0)
    {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    esp_wifi_start();
}

// HTTP GET handler for index
static esp_err_t index_get_handler(httpd_req_t *req)
{
    const char *response = "<html><body><h1>Robot Arm Control</h1>"
                           "<label for='base'>Base Angle (0-180):</label>"
                           "<input type='range' id='base' name='base' min='0' max='180' value='90' oninput='setServoAngle(\"base\", this.value)'><br>"
                           "<label for='shoulder'>Shoulder Angle (0-180):</label>"
                           "<input type='range' id='shoulder' name='shoulder' min='0' max='180' value='90' oninput='setServoAngle(\"shoulder\", this.value)'><br>"
                           "<label for='elbow'>Elbow Angle (0-180):</label>"
                           "<input type='range' id='elbow' name='elbow' min='0' max='180' value='90' oninput='setServoAngle(\"elbow\", this.value)'><br>"
                           "<label for='gripper'>Gripper Angle (0-180):</label>"
                           "<input type='range' id='gripper' name='gripper' min='0' max='180' value='90' oninput='setServoAngle(\"gripper\", this.value)'><br>"
                           "<script>"
                           "function setServoAngle(servo, angle) {"
                           "    var xhr = new XMLHttpRequest();"
                           "    xhr.open('POST', '/set_angle', true);"
                           "    xhr.setRequestHeader('Content-Type', 'application/json');"
                           "    xhr.send(JSON.stringify({servo: servo, angle: parseInt(angle)}));"
                           "}"
                           "</script>"
                           "</body></html>";
    httpd_resp_send(req, response, strlen(response));
    return ESP_OK;
}

// HTTP POST handler for setting servo angle
static esp_err_t set_angle_post_handler(httpd_req_t *req)
{
    char buf[100];
    int ret, angle;
    char *servo;

    // Receive the request payload
    if ((ret = httpd_req_recv(req, buf, MIN(req->content_len, sizeof(buf) - 1))) <= 0)
    {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT)
        {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }
    buf[ret] = '\0'; // Null-terminate the received data

    ESP_LOGI(TAG, "Received JSON: %s", buf);

    cJSON *json = cJSON_Parse(buf);
    if (json == NULL)
    {
        ESP_LOGE(TAG, "Failed to parse JSON");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *servo_json = cJSON_GetObjectItem(json, "servo");
    if (servo_json == NULL || !cJSON_IsString(servo_json))
    {
        ESP_LOGE(TAG, "Invalid servo value");
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid servo value");
        return ESP_FAIL;
    }
    servo = servo_json->valuestring;
    ESP_LOGI(TAG, "Servo: %s", servo);

    cJSON *angle_json = cJSON_GetObjectItem(json, "angle");
    if (angle_json == NULL || !cJSON_IsNumber(angle_json))
    {
        ESP_LOGE(TAG, "Invalid angle value");
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid angle value");
        return ESP_FAIL;
    }
    angle = angle_json->valueint;
    ESP_LOGI(TAG, "Angle: %d", angle);
    cJSON_Delete(json);

    if (angle < 0 || angle > 180)
    {
        ESP_LOGE(TAG, "Angle out of range");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Angle out of range");
        return ESP_FAIL;
    }

    // Debugging: Print lengths of strings being compared
    ESP_LOGI(TAG, "Comparing servo '%s' with 'base' (length %d vs %d)", servo, strlen(servo), strlen("base"));

    if (strcmp(servo, "base") == 0)
    {
        ESP_LOGI(TAG, "Setting base servo angle");
        set_servo_angle(LEDC_CHANNEL_0, angle);
    }
    else if (strcmp(servo, "shoulder") == 0)
    {
        ESP_LOGI(TAG, "Setting shoulder servo angle");
        set_servo_angle(LEDC_CHANNEL_1, angle);
    }
    else if (strcmp(servo, "elbow") == 0)
    {
        ESP_LOGI(TAG, "Setting elbow servo angle");
        set_servo_angle(LEDC_CHANNEL_2, angle);
    }
    else if (strcmp(servo, "gripper") == 0)
    {
        ESP_LOGI(TAG, "Setting gripper servo angle");
        set_servo_angle(LEDC_CHANNEL_3, angle);
    }
    else
    {
        ESP_LOGE(TAG, "Unknown servo identifier: %s", servo);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Unknown servo identifier");
        return ESP_FAIL;
    }

    const char *response = "Angle set successfully";
    httpd_resp_send(req, response, strlen(response));
    return ESP_OK;
}

// Start the web server
httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK)
    {
        httpd_uri_t index_uri = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = index_get_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &index_uri);

        httpd_uri_t set_angle_uri = {
            .uri = "/set_angle",
            .method = HTTP_POST,
            .handler = set_angle_post_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &set_angle_uri);
    }
    return server;
}

// Main application
void app_main(void)
{
    // Set log level to verbose
    // esp_log_level_set("*", ESP_LOG_VERBOSE);
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize servos
    init_servo(SERVO_PIN_BASE, LEDC_CHANNEL_0);
    init_servo(SERVO_PIN_SHOULDER, LEDC_CHANNEL_1);
    init_servo(SERVO_PIN_ELBOW, LEDC_CHANNEL_2);
    init_servo(SERVO_PIN_GRIPPER, LEDC_CHANNEL_3);

    // Set initial servo angles
    set_servo_angle(LEDC_CHANNEL_0, 90); // Base
    set_servo_angle(LEDC_CHANNEL_1, 90); // Shoulder
    set_servo_angle(LEDC_CHANNEL_2, 90); // Elbow
    set_servo_angle(LEDC_CHANNEL_3, 90); // Gripper

    ESP_LOGI(TAG, "Servos initialized and set to initial position.");

    // Initialize WiFi in AP mode
    wifi_init_ap();

    // Start the web server
    start_webserver();
}
