#include <stdio.h>
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

// WiFi credentials
#define WIFI_SSID "robot_arm"
#define WIFI_PASS "123456"

#define SERVO_PIN_BASE GPIO_NUM_27
#define SERVO_PIN_SHOULDER GPIO_NUM_26
#define SERVO_PIN_ELBOW GPIO_NUM_25
#define SERVO_PIN_GRIPPER GPIO_NUM_33

#define SERVO_MIN_PULSEWIDTH_US 500
#define SERVO_MAX_PULSEWIDTH_US 2500
#define SERVO_MAX_DEGREE 180

#define MIN(a, b) (((a) < (b)) ? (a) : (b))

static const char *TAG = "robot_arm";

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
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        esp_wifi_connect();
        ESP_LOGI(TAG, "retry to connect to the AP");
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        char ip_str[IP4ADDR_STRLEN_MAX];
        esp_ip4addr_ntoa(&event->ip_info.ip, ip_str, IP4ADDR_STRLEN_MAX);
        ESP_LOGI(TAG, "got ip:%s", ip_str);
    }
}

// Initialize WiFi
void wifi_init_sta(void)
{
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    esp_event_handler_instance_register(WIFI_EVENT,
                                        ESP_EVENT_ANY_ID,
                                        &wifi_event_handler,
                                        NULL,
                                        &instance_any_id);
    esp_event_handler_instance_register(IP_EVENT,
                                        IP_EVENT_STA_GOT_IP,
                                        &wifi_event_handler,
                                        NULL,
                                        &instance_got_ip);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS}};
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);
    esp_wifi_start();
}

// HTTP GET handler for index
static esp_err_t index_get_handler(httpd_req_t *req)
{
    const char *response = "<html><body><h1>Robot Arm Control</h1>"
                           "<form method='post' action='/set_angle'>"
                           "<label for='angle'>Angle (0-180):</label>"
                           "<input type='number' id='angle' name='angle'>"
                           "<button type='submit'>Set Angle</button>"
                           "</form></body></html>";
    httpd_resp_send(req, response, strlen(response));
    return ESP_OK;
}

// HTTP POST handler for setting servo angle
static esp_err_t set_angle_post_handler(httpd_req_t *req)
{
    char buf[100];
    int ret, angle;
    if ((ret = httpd_req_recv(req, buf, MIN(req->content_len, sizeof(buf)))) <= 0)
    {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT)
        {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON *json = cJSON_Parse(buf);
    if (json == NULL)
    {
        ESP_LOGE(TAG, "Failed to parse JSON");
        return ESP_FAIL;
    }
    cJSON *angle_json = cJSON_GetObjectItem(json, "angle");
    if (angle_json == NULL || !cJSON_IsNumber(angle_json))
    {
        ESP_LOGE(TAG, "Invalid angle value");
        cJSON_Delete(json);
        return ESP_FAIL;
    }
    angle = angle_json->valueint;
    cJSON_Delete(json);

    set_servo_angle(LEDC_CHANNEL_0, angle); // Adjust as needed for other servos

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

    // Initialize WiFi
    wifi_init_sta();

    // Start the web server
    start_webserver();
}
