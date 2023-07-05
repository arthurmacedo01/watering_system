#include <stdio.h>
#include "connect.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "mdns.h"
#include "toggleLed.h"
#include "cJSON.h"
#include "pushBtn.h"
#include "esp_spiffs.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_sntp.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "mqtt_client.h"

static const char *TAG = "SERVER";
#define AMX_APs 20

static httpd_handle_t server = NULL;
static TaskHandle_t xTask;

void timer_init(void * params);
void set_interval(int interval,int intensity);
esp_err_t send_ws_message(char *message);

typedef struct foodMoment_struct
{
  double remaining_hours;
  double interval_h;
  double intensity_perc;
} FoodMoment;

static esp_err_t on_default_url(httpd_req_t *req)
{
  ESP_LOGI(TAG, "URL: %s", req->uri);

  esp_vfs_spiffs_conf_t esp_vfs_spiffs_conf = {
      .base_path = "/spiffs",
      .partition_label = NULL,
      .max_files = 5,
      .format_if_mount_failed = true};
  esp_vfs_spiffs_register(&esp_vfs_spiffs_conf);

  char path[600];
  if (strcmp(req->uri, "/") == 0)
  {
    strcpy(path, "/spiffs/index.html");
  }
  else
  {
    sprintf(path, "/spiffs%s", req->uri);
  }
  char *ext = strrchr(path, '.');
  if (strcmp(ext, ".css") == 0)
  {
    httpd_resp_set_type(req, "text/css");
  }
  if (strcmp(ext, ".js") == 0)
  {
    httpd_resp_set_type(req, "text/javascript");
  }
  if (strcmp(ext, ".png") == 0)
  {
    httpd_resp_set_type(req, "image/png");
  }

  FILE *file = fopen(path, "r");
  if (file == NULL)
  {
    httpd_resp_send_404(req);
    esp_vfs_spiffs_unregister(NULL);
    return ESP_OK;
  }

  char lineRead[256];
  while (fgets(lineRead, sizeof(lineRead), file))
  {
    httpd_resp_sendstr_chunk(req, lineRead);
  }
  httpd_resp_sendstr_chunk(req, NULL);

  esp_vfs_spiffs_unregister(NULL);
  return ESP_OK;
}

static esp_err_t on_toggle_led_url(httpd_req_t *req)
{
  char buffer[100];
  memset(&buffer, 0, sizeof(buffer));
  httpd_req_recv(req, buffer, req->content_len);
  cJSON *payload = cJSON_Parse(buffer);
  cJSON *is_on_json = cJSON_GetObjectItem(payload, "is_on");
  cJSON *interval_json = cJSON_GetObjectItem(payload, "interval_ms");
  bool is_on = cJSON_IsTrue(is_on_json);
  int interval_ms = atoi(interval_json->valuestring);
  cJSON_Delete(payload);
  toggle_led(is_on,interval_ms);
  httpd_resp_set_status(req, "204 NO CONTENT");
  httpd_resp_send(req, NULL, 0);
  return ESP_OK;
}

static esp_err_t on_set_interval_url(httpd_req_t *req)
{
  char buffer[100];
  memset(&buffer, 0, sizeof(buffer));
  httpd_req_recv(req, buffer, req->content_len);
  cJSON *payload = cJSON_Parse(buffer);
  cJSON *remaining_json = cJSON_GetObjectItem(payload, "remaining");
  cJSON *interval_json = cJSON_GetObjectItem(payload, "interval");
  cJSON *intensity_json = cJSON_GetObjectItem(payload, "intensity");
  int remaining = atoi(remaining_json->valuestring);
  int interval = atoi(interval_json->valuestring);
  int intensity = atoi(intensity_json->valuestring);
  cJSON_Delete(payload);
  set_interval(interval,intensity);
  vTaskDelete(xTask);
  xTaskCreate(&timer_init, "food timer", 2048,"timer init", 2, &xTask);

  cJSON *payload_return = cJSON_CreateObject();

  cJSON_AddNumberToObject(payload_return, "interval", interval);
  cJSON_AddNumberToObject(payload_return, "remaining", remaining);
  cJSON_AddNumberToObject(payload_return, "intensity", intensity);
  char *response = cJSON_Print(payload_return);
  cJSON_Delete(payload_return);
  send_ws_message(response);

  httpd_resp_set_status(req, "204 NO CONTENT");
  httpd_resp_send(req, NULL, 0);
  return ESP_OK;
}

static esp_err_t on_get_ap_url(httpd_req_t *req)
{ 
  esp_wifi_set_mode(WIFI_MODE_APSTA);
  wifi_scan_config_t wifi_scan_config = {
      .bssid = 0,
      .ssid = 0,
      .channel = 0,
      .show_hidden = true};
  esp_wifi_scan_start(&wifi_scan_config, true);
  wifi_ap_record_t wifi_ap_record[AMX_APs];
  uint16_t ap_count = AMX_APs;
  esp_wifi_scan_get_ap_records(&ap_count, wifi_ap_record);
  cJSON *wifi_scan_json = cJSON_CreateArray();
  for (size_t i = 0; i < ap_count; i++)
  {
    cJSON *element = cJSON_CreateObject();
    cJSON_AddStringToObject(element, "ssid", (char *)wifi_ap_record[i].ssid);
    cJSON_AddNumberToObject(element, "rssi", wifi_ap_record[i].rssi);
    cJSON_AddItemToArray(wifi_scan_json, element);
  }
  char *json_str = cJSON_Print(wifi_scan_json);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, json_str);
  cJSON_Delete(wifi_scan_json);
  free(json_str);
  return ESP_OK;
}

/********************AP TO STA *******************/

typedef struct ap_config_t
{
  char ssid[32];
  char password[32];
} ap_config_t;

void connect_to_ap(void *params)
{
  vTaskDelay(pdMS_TO_TICKS(500));
  ap_config_t *ap_config = (ap_config_t *)params;
  wifi_disconnect();
  wifi_destroy_netif();

  if (wifi_connect_sta(ap_config->ssid, ap_config->password, 10000) != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to connect to AP");

    nvs_handle handle;
    bool flag_net = 0;
    char flag_net_key[50] = "flag_net_0";    
    size_t flag_net_Size = (size_t) sizeof(bool);
    ESP_ERROR_CHECK(nvs_open("store",NVS_READWRITE, &handle));
    ESP_ERROR_CHECK(nvs_set_blob(handle, flag_net_key, (void *) &flag_net, flag_net_Size ));
    ESP_ERROR_CHECK(nvs_commit(handle));
    nvs_close(handle);
    esp_restart();
  }
  else
  {
    ESP_LOGI(TAG, "CONNECTED TO AP");
  }
  vTaskDelete(NULL);
}

static esp_err_t on_ap_to_sta(httpd_req_t *req)
{
  static ap_config_t ap_config;

  char buffer[100];
  memset(&buffer, 0, sizeof(buffer));
  httpd_req_recv(req, buffer, req->content_len);
  cJSON *payload = cJSON_Parse(buffer);
  strcpy(ap_config.ssid, cJSON_GetObjectItem(payload, "ssid")->valuestring);
  strcpy(ap_config.password, cJSON_GetObjectItem(payload, "password")->valuestring);
  cJSON_Delete(payload);

  nvs_handle handle;
  char ap_config_key[50] = "ap_config_0";
  size_t ap_config_Size = (size_t) sizeof(ap_config_t);
  ESP_ERROR_CHECK(nvs_open("store",NVS_READWRITE, &handle));
  ESP_ERROR_CHECK(nvs_set_blob(handle, ap_config_key, (void *) &ap_config, ap_config_Size ));
  ESP_ERROR_CHECK(nvs_commit(handle));

  bool flag_net = 1;
  char flag_net_key[50] = "flag_net_0";    
  size_t flag_net_Size = (size_t) sizeof(bool);
  ESP_ERROR_CHECK(nvs_set_blob(handle, flag_net_key, (void *) &flag_net, flag_net_Size ));
  ESP_ERROR_CHECK(nvs_commit(handle));

  nvs_close(handle);

  httpd_resp_set_status(req, "204 NO CONTENT");
  httpd_resp_send(req, NULL, 0);

  xTaskCreate(connect_to_ap, "connect_to_ap", 1024 * 5, &ap_config, 1, NULL);

  return ESP_OK;
}




/********************Web Socket *******************/

#define WS_MAX_SIZE 1024
static int client_session_id;

esp_err_t send_ws_message(char *message)
{
  if (!client_session_id)
  {
    ESP_LOGE(TAG, "no client_session_id");
    return -1;
  }
  httpd_ws_frame_t ws_message = {
      .final = true,
      .fragmented = false,
      .len = strlen(message),
      .payload = (uint8_t *)message,
      .type = HTTPD_WS_TYPE_TEXT};
  return httpd_ws_send_frame_async(server, client_session_id, &ws_message);
}

static esp_err_t on_web_socket_url(httpd_req_t *req)
{
  client_session_id = httpd_req_to_sockfd(req);
  if (req->method == HTTP_GET)
    return ESP_OK;

  httpd_ws_frame_t ws_pkt;
  memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
  ws_pkt.type = HTTPD_WS_TYPE_TEXT;
  ws_pkt.payload = malloc(WS_MAX_SIZE);
  httpd_ws_recv_frame(req, &ws_pkt, WS_MAX_SIZE);
  printf("ws payload: %.*s\n", ws_pkt.len, ws_pkt.payload);
  free(ws_pkt.payload);

  nvs_handle handle;
  ESP_ERROR_CHECK(nvs_open("store",NVS_READWRITE, &handle));
  FoodMoment foodMoment;
  char foodMoment_key[50] = "food_0";
  size_t foodMomentSize = (size_t) sizeof(FoodMoment);

  nvs_get_blob(handle,foodMoment_key,(void *) &foodMoment, &foodMomentSize);
  cJSON *payload = cJSON_CreateObject();
  cJSON_AddNumberToObject(payload, "interval", foodMoment.interval_h);
  cJSON_AddNumberToObject(payload, "remaining", foodMoment.remaining_hours);
  cJSON_AddNumberToObject(payload, "intensity", foodMoment.intensity_perc);
  char *response = cJSON_Print(payload);

  // char *response = "connected OK";
  httpd_ws_frame_t ws_response = {
      .final = true,
      .fragmented = false,
      .type = HTTPD_WS_TYPE_TEXT,
      .payload = (uint8_t *)response,
      .len = strlen(response)};
  return httpd_ws_send_frame(req, &ws_response);
}

/*******************************************/

static void init_server()
{

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.uri_match_fn = httpd_uri_match_wildcard;

  ESP_ERROR_CHECK(httpd_start(&server, &config));

  httpd_uri_t get_ap_list_url = {
      .uri = "/api/get-ap-list",
      .method = HTTP_GET,
      .handler = on_get_ap_url};
  httpd_register_uri_handler(server, &get_ap_list_url);

  httpd_uri_t toggle_led_url = {
      .uri = "/api/toggle-led",
      .method = HTTP_POST,
      .handler = on_toggle_led_url};
  httpd_register_uri_handler(server, &toggle_led_url);

  httpd_uri_t set_interval_url = {
      .uri = "/api/set-interval",
      .method = HTTP_POST,
      .handler = on_set_interval_url};
  httpd_register_uri_handler(server, &set_interval_url);


  httpd_uri_t ap_to_sta_url = {
      .uri = "/api/ap-sta",
      .method = HTTP_POST,
      .handler = on_ap_to_sta};
  httpd_register_uri_handler(server, &ap_to_sta_url);

  httpd_uri_t web_socket_url = {
      .uri = "/ws",
      .method = HTTP_GET,
      .handler = on_web_socket_url,
      .is_websocket = true};
  httpd_register_uri_handler(server, &web_socket_url);

  httpd_uri_t default_url = {
      .uri = "/*",
      .method = HTTP_GET,
      .handler = on_default_url};
  httpd_register_uri_handler(server, &default_url);
}

void start_mdns_service()
{
  mdns_init();
  mdns_hostname_set("alimentador");
  mdns_instance_name_set("Alimentador");
}

void get_ap_config_try_to_connect(void)
{
  static ap_config_t ap_config;
  nvs_handle handle;
  char ap_config_key[50] = "ap_config_0";
  size_t ap_config_Size = (size_t) sizeof(ap_config_t);
  ESP_ERROR_CHECK(nvs_open("store",NVS_READWRITE, &handle));
  esp_err_t result = nvs_get_blob(handle,ap_config_key,(void *) &ap_config, &ap_config_Size);

  bool flag_net;
  char flag_net_key[50] = "flag_net_0";    
  size_t flag_net_Size = (size_t) sizeof(bool);
  nvs_get_blob(handle,flag_net_key,(void *) &flag_net, &flag_net_Size);
  switch (result)
  {
  case ESP_ERR_NOT_FOUND:
  case ESP_ERR_NVS_NOT_FOUND:
    ESP_LOGE(TAG, "Value not set yet");
    strcpy(ap_config.ssid, "Mar");
    strcpy(ap_config.password, "apartamento");
    ESP_ERROR_CHECK(nvs_set_blob(handle, ap_config_key, (void *) &ap_config, ap_config_Size ));
    ESP_ERROR_CHECK(nvs_commit(handle));
    flag_net = 0;
    ESP_ERROR_CHECK(nvs_set_blob(handle, flag_net_key, (void *) &flag_net, flag_net_Size ));
    ESP_ERROR_CHECK(nvs_commit(handle));
    break;
  case ESP_OK:
    break;
  default:
    ESP_LOGE(TAG, "Error (%s) opening NVS handle!\n", esp_err_to_name(result));
    break;
  }
  nvs_close(handle);

  if(flag_net){
    xTaskCreate(connect_to_ap, "connect_to_ap", 1024 * 5, &ap_config, 1, NULL);
  }
  else{
    wifi_connect_ap("alimentador", "alimentador");
  }

  return;
}


void timer_init(void * params)
{
  nvs_handle handle;
  ESP_ERROR_CHECK(nvs_open("store",NVS_READWRITE, &handle));
  
  FoodMoment foodMoment;
  char foodMoment_key[50] = "food_0";
  size_t foodMomentSize = (size_t) sizeof(FoodMoment);
  esp_err_t result = nvs_get_blob(handle,foodMoment_key,(void *) &foodMoment, &foodMomentSize);
  switch (result)
  {
  case ESP_ERR_NOT_FOUND:
  case ESP_ERR_NVS_NOT_FOUND:
    ESP_LOGE(TAG, "Value not set yet");
    foodMoment.intensity_perc = 50;
    foodMoment.interval_h = 24;
    foodMoment.remaining_hours = 24;
    ESP_ERROR_CHECK(nvs_set_blob(handle, foodMoment_key, (void *) &foodMoment, foodMomentSize ));
    ESP_ERROR_CHECK(nvs_commit(handle));
    break;
  case ESP_OK:
    // ESP_LOGI(TAG, "intensity_perc: %f interval_h: %f remaining: %f", foodMoment.intensity_perc, foodMoment.interval_h, foodMoment.remaining_hours);
    break;
  default:
    ESP_LOGE(TAG, "Error (%s) opening NVS handle!\n", esp_err_to_name(result));
    break;
  }
  nvs_close(handle);
  while (true)
  {
    vTaskDelay(1000*60*60 / portTICK_PERIOD_MS);
    foodMoment.remaining_hours--;
    if(foodMoment.remaining_hours<1)
    {
      toggle_led(true, foodMoment.intensity_perc);
      foodMoment.remaining_hours = foodMoment.interval_h;
    }
    ESP_LOGI(TAG,"remaining: %f total: %f",foodMoment.remaining_hours,foodMoment.interval_h);
    ESP_ERROR_CHECK(nvs_open("store",NVS_READWRITE, &handle));
    ESP_ERROR_CHECK(nvs_set_blob(handle, foodMoment_key, (void *) &foodMoment, foodMomentSize ));
    ESP_ERROR_CHECK(nvs_commit(handle));
    nvs_close(handle);

    cJSON *payload = cJSON_CreateObject();
    cJSON_AddNumberToObject(payload, "interval", foodMoment.interval_h);
    cJSON_AddNumberToObject(payload, "remaining", foodMoment.remaining_hours);
    cJSON_AddNumberToObject(payload, "intensity", foodMoment.intensity_perc);
    char *message = cJSON_Print(payload);
    // printf("message: %s\n", message);
    send_ws_message(message);
    cJSON_Delete(payload);
    free(message);
  }
}

void set_interval(int interval,int intensity)
{
  FoodMoment foodMoment;
  foodMoment.intensity_perc = intensity;
  foodMoment.interval_h = interval;
  foodMoment.remaining_hours = interval;
  
  nvs_handle handle;
  char foodMoment_key[50] = "food_0";
  size_t foodMomentSize = (size_t) sizeof(FoodMoment);
  ESP_ERROR_CHECK(nvs_open("store",NVS_READWRITE, &handle));
  ESP_ERROR_CHECK(nvs_set_blob(handle, foodMoment_key, (void *) &foodMoment, foodMomentSize ));
  ESP_ERROR_CHECK(nvs_commit(handle));
  nvs_close(handle);
} 

/*
 * @brief Event handler registered to receive MQTT events
 *
 *  This function is called by the MQTT client event loop.
 *
 * @param handler_args user data registered to the event.
 * @param base Event base for the handler(always MQTT Base in this example).
 * @param event_id The id for the received event.
 * @param event_data The data for the event, esp_mqtt_event_handle_t.
 */
static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        msg_id = esp_mqtt_client_publish(client, "/topic/alimentador", "ESP_conected", 0, 1, 0);
        ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_subscribe(client, "/topic/alimentador", 0);
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);        
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        ESP_LOGI(TAG, "TOPIC: %.*s DATA =  %.*s",event->topic_len, event->topic,event->data_len, event->data);      
        char *buffer = malloc(event->data_len);      
        strncpy(buffer, event->data,event->data_len);
        cJSON *payload = cJSON_Parse(buffer);
        free(buffer);
        cJSON *intensity_json = cJSON_GetObjectItem(payload, "intensity");
         int intensity = atoi(intensity_json->valuestring);
        cJSON_Delete(payload);
        toggle_led(0,intensity);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno",  event->error_handle->esp_transport_sock_errno);
            ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));

        }
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

static void mqtt_app_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .uri = CONFIG_BROKER_URL,
    };
#if CONFIG_BROKER_URL_FROM_STDIN
    char line[128];

    if (strcmp(mqtt_cfg.uri, "FROM_STDIN") == 0) {
        int count = 0;
        printf("Please enter url of mqtt broker\n");
        while (count < 128) {
            int c = fgetc(stdin);
            if (c == '\n') {
                line[count] = '\0';
                break;
            } else if (c > 0 && c < 127) {
                line[count] = c;
                ++count;
            }
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
        mqtt_cfg.uri = line;
        printf("Broker url: %s\n", line);
    } else {
        ESP_LOGE(TAG, "Configuration mismatch: wrong broker url");
        abort();
    }
#endif /* CONFIG_BROKER_URL_FROM_STDIN */

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
}


void app_main(void)
{
  ESP_ERROR_CHECK(nvs_flash_init());
  esp_log_level_set("*", ESP_LOG_INFO);
  esp_log_level_set("MQTT_CLIENT", ESP_LOG_VERBOSE);
  esp_log_level_set("MQTT_EXAMPLE", ESP_LOG_VERBOSE);
  esp_log_level_set("TRANSPORT_BASE", ESP_LOG_VERBOSE);
  esp_log_level_set("esp-tls", ESP_LOG_VERBOSE);
  esp_log_level_set("TRANSPORT", ESP_LOG_VERBOSE);
  esp_log_level_set("OUTBOX", ESP_LOG_VERBOSE);

  init_led();
  init_btn();
  wifi_init();
  get_ap_config_try_to_connect();
  xTaskCreate(&timer_init, "food timer", 2048,"timer init", 2, &xTask);
  start_mdns_service();
  init_server();
  mqtt_app_start();
}