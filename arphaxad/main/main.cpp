/* Blink C++ Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

#include <nmeaparse/nmea.h>

#include "esp_check.h"
#include "esp_chip_info.h"
#include "esp_eth.h"
#include "esp_event.h"
#include "esp_flash.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_tls_crypto.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gpio_cxx.hpp"
#include "nvs_flash.h"
#include <sys/param.h>
#include <sys/time.h>
#include <time.h>

#include "lwip/err.h"
#include "lwip/sys.h"

#include "esp_exception.hpp"
#include "esp_timer_cxx.hpp"

/* The examples use WiFi configuration that you can set via project
   configuration menu.

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/
#define EXAMPLE_ESP_WIFI_SSID CONFIG_ESP_WIFI_SSID
#define EXAMPLE_ESP_WIFI_PASS CONFIG_ESP_WIFI_PASSWORD
#define EXAMPLE_ESP_WIFI_CHANNEL CONFIG_ESP_WIFI_CHANNEL
#define EXAMPLE_MAX_STA_CONN CONFIG_ESP_MAX_STA_CONN

#if CONFIG_ESP_GTK_REKEYING_ENABLE
#define EXAMPLE_GTK_REKEY_INTERVAL CONFIG_ESP_GTK_REKEY_INTERVAL
#else
#define EXAMPLE_GTK_REKEY_INTERVAL 0
#endif

#define EXAMPLE_HTTP_QUERY_KEY_MAX_LEN (64)

using namespace std;
using namespace nmea;
using namespace idf;
using namespace idf::esp_timer;

static const char *TAG = "randop";

double gps_latitude = 0.0;
double gps_longitude = 0.0;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
  if (event_id == WIFI_EVENT_AP_STACONNECTED) {
    wifi_event_ap_staconnected_t *event =
        (wifi_event_ap_staconnected_t *)event_data;
    ESP_LOGI(TAG, "station " MACSTR " join, AID=%d", MAC2STR(event->mac),
             event->aid);
  } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
    wifi_event_ap_stadisconnected_t *event =
        (wifi_event_ap_stadisconnected_t *)event_data;
    ESP_LOGI(TAG, "station " MACSTR " leave, AID=%d, reason=%d",
             MAC2STR(event->mac), event->aid, event->reason);
  }
}

void wifi_init_softap(void) {
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_ap();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));

  wifi_config_t wifi_config{};
  wifi_config.ap.ssid_len = strlen(EXAMPLE_ESP_WIFI_SSID);
  memcpy(wifi_config.ap.ssid, EXAMPLE_ESP_WIFI_SSID, wifi_config.ap.ssid_len);

  strcpy((char *)wifi_config.ap.password,
         EXAMPLE_ESP_WIFI_PASS); // or memcpy + zero rest
  wifi_config.ap.password[sizeof(wifi_config.ap.password) - 1] =
      0; // ensure null-terminated if needed

  wifi_config.ap.channel = EXAMPLE_ESP_WIFI_CHANNEL;
  wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
  wifi_config.ap.max_connection = EXAMPLE_MAX_STA_CONN;
  wifi_config.ap.gtk_rekey_interval = EXAMPLE_GTK_REKEY_INTERVAL;

  if (strlen(EXAMPLE_ESP_WIFI_PASS) == 0) {
    wifi_config.ap.authmode = WIFI_AUTH_OPEN;
  }

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s channel:%d",
           EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS,
           EXAMPLE_ESP_WIFI_CHANNEL);
}

#if CONFIG_EXAMPLE_BASIC_AUTH

typedef struct {
  char *username;
  char *password;
} basic_auth_info_t;

#define HTTPD_401 "401 UNAUTHORIZED" /*!< HTTP Response 401 */

static char *http_auth_basic(const char *username, const char *password) {
  size_t out;
  char *user_info = NULL;
  char *digest = NULL;
  size_t n = 0;
  int rc = asprintf(&user_info, "%s:%s", username, password);
  if (rc < 0) {
    ESP_LOGE(TAG, "asprintf() returned: %d", rc);
    return NULL;
  }

  if (!user_info) {
    ESP_LOGE(TAG, "No enough memory for user information");
    return NULL;
  }
  esp_crypto_base64_encode(NULL, 0, &n, (const unsigned char *)user_info,
                           strlen(user_info));

  /* 6: The length of the "Basic " string
   * n: Number of bytes for a base64 encode format
   * 1: Number of bytes for a reserved which be used to fill zero
   */
  digest = calloc(1, 6 + n + 1);
  if (digest) {
    strcpy(digest, "Basic ");
    esp_crypto_base64_encode((unsigned char *)digest + 6, n, &out,
                             (const unsigned char *)user_info,
                             strlen(user_info));
  }
  free(user_info);
  return digest;
}

/* An HTTP GET handler */
static esp_err_t basic_auth_get_handler(httpd_req_t *req) {
  char *buf = NULL;
  size_t buf_len = 0;
  basic_auth_info_t *basic_auth_info = req->user_ctx;

  buf_len = httpd_req_get_hdr_value_len(req, "Authorization") + 1;
  if (buf_len > 1) {
    buf = calloc(1, buf_len);
    if (!buf) {
      ESP_LOGE(TAG, "No enough memory for basic authorization");
      return ESP_ERR_NO_MEM;
    }

    if (httpd_req_get_hdr_value_str(req, "Authorization", buf, buf_len) ==
        ESP_OK) {
      ESP_LOGI(TAG, "Found header => Authorization: %s", buf);
    } else {
      ESP_LOGE(TAG, "No auth value received");
    }

    char *auth_credentials =
        http_auth_basic(basic_auth_info->username, basic_auth_info->password);
    if (!auth_credentials) {
      ESP_LOGE(TAG, "No enough memory for basic authorization credentials");
      free(buf);
      return ESP_ERR_NO_MEM;
    }

    if (strncmp(auth_credentials, buf, buf_len)) {
      ESP_LOGE(TAG, "Not authenticated");
      httpd_resp_set_status(req, HTTPD_401);
      httpd_resp_set_type(req, "application/json");
      httpd_resp_set_hdr(req, "Connection", "keep-alive");
      httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"Hello\"");
      httpd_resp_send(req, NULL, 0);
    } else {
      ESP_LOGI(TAG, "Authenticated!");
      char *basic_auth_resp = NULL;
      httpd_resp_set_status(req, HTTPD_200);
      httpd_resp_set_type(req, "application/json");
      httpd_resp_set_hdr(req, "Connection", "keep-alive");
      int rc = asprintf(&basic_auth_resp,
                        "{\"authenticated\": true,\"user\": \"%s\"}",
                        basic_auth_info->username);
      if (rc < 0) {
        ESP_LOGE(TAG, "asprintf() returned: %d", rc);
        free(auth_credentials);
        return ESP_FAIL;
      }
      if (!basic_auth_resp) {
        ESP_LOGE(TAG, "No enough memory for basic authorization response");
        free(auth_credentials);
        free(buf);
        return ESP_ERR_NO_MEM;
      }
      httpd_resp_send(req, basic_auth_resp, strlen(basic_auth_resp));
      free(basic_auth_resp);
    }
    free(auth_credentials);
    free(buf);
  } else {
    ESP_LOGE(TAG, "No auth header received");
    httpd_resp_set_status(req, HTTPD_401);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Connection", "keep-alive");
    httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"Hello\"");
    httpd_resp_send(req, NULL, 0);
  }

  return ESP_OK;
}

static httpd_uri_t basic_auth = {
    .uri = "/basic_auth",
    .method = HTTP_GET,
    .handler = basic_auth_get_handler,
};

static void httpd_register_basic_auth(httpd_handle_t server) {
  basic_auth_info_t *basic_auth_info = calloc(1, sizeof(basic_auth_info_t));
  if (basic_auth_info) {
    basic_auth_info->username = CONFIG_EXAMPLE_BASIC_AUTH_USERNAME;
    basic_auth_info->password = CONFIG_EXAMPLE_BASIC_AUTH_PASSWORD;

    basic_auth.user_ctx = basic_auth_info;
    httpd_register_uri_handler(server, &basic_auth);
  }
}
#endif

/* An HTTP GET handler */
static esp_err_t hello_get_handler(httpd_req_t *req) {
  char *buf;
  size_t buf_len;

  /* Get header value string length and allocate memory for length + 1,
   * extra byte for null termination */
  buf_len = httpd_req_get_hdr_value_len(req, "Host") + 1;
  if (buf_len > 1) {
    buf = static_cast<char *>(malloc(buf_len));
    ESP_RETURN_ON_FALSE(buf, ESP_ERR_NO_MEM, TAG, "buffer alloc failed");
    /* Copy null terminated value string into buffer */
    if (httpd_req_get_hdr_value_str(req, "Host", buf, buf_len) == ESP_OK) {
      // ESP_LOGI(TAG, "Found header => Host: %s", buf);
    }
    free(buf);
  }

  /* Read URL query string length and allocate memory for length + 1,
   * extra byte for null termination */
  buf_len = httpd_req_get_url_query_len(req) + 1;
  if (buf_len > 1) {
    buf = static_cast<char *>(malloc(buf_len));
    ESP_RETURN_ON_FALSE(buf, ESP_ERR_NO_MEM, TAG, "buffer alloc failed");
    if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
      ESP_LOGI(TAG, "Found URL query => %s", buf);
      char param[EXAMPLE_HTTP_QUERY_KEY_MAX_LEN],
          dec_param[EXAMPLE_HTTP_QUERY_KEY_MAX_LEN] = {0};
      /* Get value of expected key from query string */
      if (httpd_query_key_value(buf, "query1", param, sizeof(param)) ==
          ESP_OK) {
        ESP_LOGI(TAG, "Found URL query parameter => query1=%s", param);
        ESP_LOGI(TAG, "Decoded query parameter => %s", dec_param);
      }
      if (httpd_query_key_value(buf, "query3", param, sizeof(param)) ==
          ESP_OK) {
        ESP_LOGI(TAG, "Found URL query parameter => query3=%s", param);
        ESP_LOGI(TAG, "Decoded query parameter => %s", dec_param);
      }
      if (httpd_query_key_value(buf, "query2", param, sizeof(param)) ==
          ESP_OK) {
        ESP_LOGI(TAG, "Found URL query parameter => query2=%s", param);
        ESP_LOGI(TAG, "Decoded query parameter => %s", dec_param);
      }
    }
    free(buf);
  }

  /* Set some custom headers */
  // httpd_resp_set_hdr(req, "Custom-Header-1", "Custom-Value-1");
  httpd_resp_set_hdr(req, "X-Server", "Randolph");

  /* Send response with custom headers and body set as the
   * string passed in user context*/
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(8) << "{\"latitude\":" << gps_latitude
      << ",\"longitude\":" << gps_longitude << "}";

  std::string resp_gps = oss.str();
  const char *resp_str = resp_gps.c_str();

  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);

  return ESP_OK;
}

static const httpd_uri_t hello = {.uri = "/gps",
                                  .method = HTTP_GET,
                                  .handler = hello_get_handler,
                                  /* Let's pass response string in user
                                   * context to demonstrate it's usage */
                                  .user_ctx = (void *)"Hello World!"};

/* An HTTP POST handler */
static esp_err_t echo_post_handler(httpd_req_t *req) {
  char buf[100];
  int ret, remaining = req->content_len;

  while (remaining > 0) {
    /* Read the data for the request */
    if ((ret = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)))) <= 0) {
      if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
        /* Retry receiving if timeout occurred */
        continue;
      }
      return ESP_FAIL;
    }

    /* Send back the same data */
    httpd_resp_send_chunk(req, buf, ret);
    remaining -= ret;

    /* Log data received */
    ESP_LOGI(TAG, "=========== RECEIVED DATA ==========");
    ESP_LOGI(TAG, "%.*s", ret, buf);
    ESP_LOGI(TAG, "====================================");
  }

  // End response
  httpd_resp_send_chunk(req, NULL, 0);
  return ESP_OK;
}

static const httpd_uri_t echo = {.uri = "/echo",
                                 .method = HTTP_POST,
                                 .handler = echo_post_handler,
                                 .user_ctx = NULL};

/* An HTTP_ANY handler */
static esp_err_t any_handler(httpd_req_t *req) {
  /* Send response with body set as the
   * string passed in user context*/
  const char *resp_str = (const char *)req->user_ctx;
  httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);

  // End response
  httpd_resp_send_chunk(req, NULL, 0);
  return ESP_OK;
}

static const httpd_uri_t any = {.uri = "/any",
                                .method = HTTP_GET,
                                .handler = any_handler,
                                /* Let's pass response string in user
                                 * context to demonstrate it's usage */
                                .user_ctx = (void *)"Hello World!"};

/* This handler allows the custom error handling functionality to be
 * tested from client side. For that, when a PUT request 0 is sent to
 * URI /ctrl, the /hello and /echo URIs are unregistered and following
 * custom error handler http_404_error_handler() is registered.
 * Afterwards, when /hello or /echo is requested, this custom error
 * handler is invoked which, after sending an error message to client,
 * either closes the underlying socket (when requested URI is /echo)
 * or keeps it open (when requested URI is /hello). This allows the
 * client to infer if the custom error handler is functioning as expected
 * by observing the socket state.
 */
esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err) {
  if (strcmp("/hello", req->uri) == 0) {
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND,
                        "/hello URI is not available");
    /* Return ESP_OK to keep underlying socket open */
    return ESP_OK;
  } else if (strcmp("/echo", req->uri) == 0) {
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "/echo URI is not available");
    /* Return ESP_FAIL to close underlying socket */
    return ESP_FAIL;
  }
  /* For any other URI send 404 and close socket */
  httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Some 404 error message");
  return ESP_FAIL;
}

/* An HTTP PUT handler. This demonstrates realtime
 * registration and deregistration of URI handlers
 */
static esp_err_t ctrl_put_handler(httpd_req_t *req) {
  char buf;
  int ret;

  if ((ret = httpd_req_recv(req, &buf, 1)) <= 0) {
    if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
      httpd_resp_send_408(req);
    }
    return ESP_FAIL;
  }

  if (buf == '0') {
    /* URI handlers can be unregistered using the uri string */
    ESP_LOGI(TAG, "Unregistering /hello and /echo URIs");
    httpd_unregister_uri(req->handle, "/hello");
    httpd_unregister_uri(req->handle, "/echo");
    /* Register the custom error handler */
    httpd_register_err_handler(req->handle, HTTPD_404_NOT_FOUND,
                               http_404_error_handler);
  } else {
    ESP_LOGI(TAG, "Registering /hello and /echo URIs");
    httpd_register_uri_handler(req->handle, &hello);
    httpd_register_uri_handler(req->handle, &echo);
    /* Unregister custom error handler */
    httpd_register_err_handler(req->handle, HTTPD_404_NOT_FOUND, NULL);
  }

  /* Respond with empty body */
  httpd_resp_send(req, NULL, 0);
  return ESP_OK;
}

static const httpd_uri_t ctrl = {.uri = "/ctrl",
                                 .method = HTTP_PUT,
                                 .handler = ctrl_put_handler,
                                 .user_ctx = NULL};

#if CONFIG_EXAMPLE_ENABLE_SSE_HANDLER
/* An HTTP GET handler for SSE */
static esp_err_t sse_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/event-stream");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
  httpd_resp_set_hdr(req, "Connection", "keep-alive");

  char sse_data[64];
  while (1) {
    struct timeval tv;
    gettimeofday(&tv, NULL);             // Get the current time
    int64_t time_since_boot = tv.tv_sec; // Time since boot in seconds
    esp_err_t err;
    int len = snprintf(sse_data, sizeof(sse_data),
                       "data: Time since boot: %" PRIi64 " seconds\n\n",
                       time_since_boot);
    if ((err = httpd_resp_send_chunk(req, sse_data, len)) != ESP_OK) {
      ESP_LOGE(TAG, "Failed to send sse data (returned %02X)", err);
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(1000)); // Send data every second
  }

  httpd_resp_send_chunk(req, NULL, 0); // End response
  return ESP_OK;
}

static const httpd_uri_t sse = {.uri = "/sse",
                                .method = HTTP_GET,
                                .handler = sse_handler,
                                .user_ctx = NULL};
#endif // CONFIG_EXAMPLE_ENABLE_SSE_HANDLER

static httpd_handle_t start_webserver(void) {
  httpd_handle_t server = NULL;
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
#if CONFIG_IDF_TARGET_LINUX
  // Setting port as 8001 when building for Linux. Port 80 can be used only by a
  // privileged user in linux. So when a unprivileged user tries to run the
  // application, it throws bind error and the server is not started. Port 8001
  // can be used by an unprivileged user as well. So the application will not
  // throw bind error and the server will be started.
  config.server_port = 8001;
#endif // !CONFIG_IDF_TARGET_LINUX
  config.lru_purge_enable = true;

  // Start the httpd server
  ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
  if (httpd_start(&server, &config) == ESP_OK) {
    // Set URI handlers
    ESP_LOGI(TAG, "Registering URI handlers");
    httpd_register_uri_handler(server, &hello);
    httpd_register_uri_handler(server, &echo);
    httpd_register_uri_handler(server, &ctrl);
    httpd_register_uri_handler(server, &any);
#if CONFIG_EXAMPLE_ENABLE_SSE_HANDLER
    httpd_register_uri_handler(server, &sse); // Register SSE handler
#endif
#if CONFIG_EXAMPLE_BASIC_AUTH
    httpd_register_basic_auth(server);
#endif
    return server;
  }

  ESP_LOGI(TAG, "Error starting server!");
  return NULL;
}

#if !CONFIG_IDF_TARGET_LINUX
static esp_err_t stop_webserver(httpd_handle_t server) {
  // Stop the httpd server
  return httpd_stop(server);
}

static void disconnect_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
  httpd_handle_t *server = (httpd_handle_t *)arg;
  if (*server) {
    ESP_LOGI(TAG, "Stopping webserver");
    if (stop_webserver(*server) == ESP_OK) {
      *server = NULL;
    } else {
      ESP_LOGE(TAG, "Failed to stop http server");
    }
  }
}

static void connect_handler(void *arg, esp_event_base_t event_base,
                            int32_t event_id, void *event_data) {
  httpd_handle_t *server = (httpd_handle_t *)arg;
  if (*server == NULL) {
    ESP_LOGI(TAG, "Starting webserver");
    *server = start_webserver();
  }
}
#endif // !CONFIG_IDF_TARGET_LINUX

extern "C" void app_main(void) {
  ESP_LOGI(TAG, "Arphaxad startup NMEA GPS");

  // Initialize NVS
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  ESP_LOGI(TAG, "ESP_WIFI_MODE_AP");
  wifi_init_softap();

  /* Print chip information */
  esp_chip_info_t chip_info;
  uint32_t flash_size;
  esp_chip_info(&chip_info);

  int chip_cores = chip_info.cores;
  cout << "This is " << CONFIG_IDF_TARGET << " chip with " << chip_cores
       << " CPU core(s), "
       << ((chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi/" : "")
       << ((chip_info.features & CHIP_FEATURE_BT) ? "BT" : "")
       << ((chip_info.features & CHIP_FEATURE_BLE) ? "BLE" : "")
       << ((chip_info.features & CHIP_FEATURE_IEEE802154)
               ? ", 802.15.4 (Zigbee/Thread)"
               : "")
       << ", ";

  unsigned major_rev = chip_info.revision / 100;
  unsigned minor_rev = chip_info.revision % 100;

  cout << "silicon revision v" << major_rev << "." << minor_rev << ",";

  if (esp_flash_get_size(NULL, &flash_size) != ESP_OK) {
    cout << "Get flash size failed" << endl;
    return;
  }

  cout << " " << flash_size / (uint32_t)(1024 * 1024) << "MB "
       << ((chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded"
                                                         : "external")
       << " flash" << endl;

  cout << "Minimum free heap size: " << esp_get_minimum_free_heap_size()
       << " bytes" << endl;

  try {
    cout << "Setting up timer to trigger in 500ms" << endl;
    ESPTimer timer([]() { cout << "timeout" << endl; });
    timer.start(chrono::microseconds(200 * 1000));

    this_thread::sleep_for(std::chrono::milliseconds(550));

    cout << "Setting up timer to trigger periodically every 200ms" << endl;
    ESPTimer timer2([]() {
      cout << "periodic timeout" << endl;
      fflush(stdout);
    });
    timer2.start_periodic(chrono::microseconds(200 * 1000));

    this_thread::sleep_for(std::chrono::milliseconds(1050));
  } catch (const ESPException &e) {
    cout << "Timer Exception with error: " << e.error << endl;
  }

  /** Example GPS: SM North Edsa, Philippines **/
  const char bytestream[] = "$GPGGA,143528.00,1439.42000,N,12101.62000,E,1,09,"
                            "0.92,48.2,M,48.2,M,,*69\r\n";

  NMEAParser parser;
  GPSService gps(parser);

  parser.onSentence += [](const NMEASentence &nmea) {
    cout << "GPS Received $" << nmea.name << endl;
  };

  static httpd_handle_t server = NULL;
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                             &connect_handler, &server));
  ESP_ERROR_CHECK(esp_event_handler_register(
      WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnect_handler, &server));
  server = start_webserver();

  /* The functions of GPIO_Output throws exceptions in case of parameter errors
     or if there are underlying driver errors. */
  try {
    /* This line may throw an exception if the pin number is invalid. */
    const GPIO_Output gpio(GPIONum(2));

    gps.onUpdate += [&gps, &gpio]() {
      if (gps.fix.locked()) {
        gps_latitude = gps.fix.latitude;
        gps_longitude = gps.fix.longitude;

        cout << " # Position: " << gps.fix.latitude << ", " << gps.fix.longitude
             << endl;

        gpio.set_high();
        this_thread::sleep_for(std::chrono::milliseconds(250));
        gpio.set_low();

      } else {
        cout << " # Searching..." << endl;
      }
    };

  } catch (GPIOException &e) {
    cout << "GPIO exception occurred: " << esp_err_to_name(e.error) << endl;
  }

  this_thread::sleep_for(std::chrono::seconds(1));

  try {
    parser.readBuffer((uint8_t *)bytestream, sizeof(bytestream));
  } catch (NMEAParseError &err) {
    cout << "Error: " << err.message << endl;
  }

  while (server) {
    this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}
