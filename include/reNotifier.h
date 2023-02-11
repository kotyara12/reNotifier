/* 
   EN: Sending notifications about system and service failures to Telegram or mail
   RU: Отправка уведомлений об отказах систем и служб в Telegram или на почту
   --------------------------
   (с) 2022 Разживин Александр | Razzhivin Alexander
   kotyara12@yandex.ru | https://kotyara12.ru | tg: @kotyara1971
*/

#ifndef __RENOTIFIER_H__
#define __RENOTIFIER_H__

#include <sys/types.h>
#include <time.h>
#include <math.h>
#include "esp_err.h"
#include "esp_timer.h"
#include "project_config.h"
#include "def_consts.h"
#include "rTypes.h"
#include "reEsp32.h"

class reHealthMonitor;

typedef enum {
  HM_NONE = 0,        // No notification
  HM_FAILURE,         // Only in case of service failure
  HM_RECOVERY,        // Only when the service is restored, without considering the previous failure notification
  HM_AUTO,            // When restoring the service, if there was a previous failure notification
  HM_FORCED           // In both cases
} hm_notify_mode_t;

typedef struct {
  reHealthMonitor* monitor;
  const char* object;
  const char* msg_template;
  msg_options_t msg_options;
  esp_err_t state;
  time_t time_state;
  time_t time_failure;
} hm_notify_data_t;

typedef bool (*hm_send_notify_t) (hm_notify_data_t *notify_data);

class reHealthMonitor {
  public:
    reHealthMonitor(const char* service, hm_notify_mode_t notify_mode, 
      msg_options_t msg_options, const char* msg_ok, const char* msg_failure, 
      uint8_t failure_thresold, hm_send_notify_t cb_notify);
    ~reHealthMonitor();

    // Attach external parameters
    void assignParams(uint32_t* failure_confirm_timeout, uint8_t* enable_notify);

    // Set a new state (error code) and send a notification immediately or with a delay (possibly)
    void setStateCustom(esp_err_t new_state, time_t time_state, bool forced_send, char* ext_object);
    void setState(esp_err_t new_state, time_t time_state);
    
    // Early (before the timer expires) sending a notification
    void forcedTimeout();

    // Blocking notifications for an external reason
    void lock();
    void unlock();
    bool isLocked();

    void timerTimeout();
  private:
    hm_notify_mode_t _mode = HM_NONE;
    const char* _service = nullptr;    
    const char* _msg_ok = nullptr;
    const char* _msg_failure = nullptr;
    char* _object = nullptr;
    msg_options_t _msg_options = 0;
    esp_err_t _state = ESP_OK;
    uint8_t _fail_thresold = 0;
    uint8_t _fail_count = 0;
    time_t _time_state = 0;
    time_t _time_failure = 0;
    hm_send_notify_t _notify_cb = nullptr;
    esp_timer_handle_t _notify_timer = nullptr;
    uint32_t* _notify_delay = nullptr;
    uint8_t* _notify_enable = nullptr;
    uint8_t _notify_flags = 0;

    bool timerStart();
    bool timerStop();
    bool sendNotifyPrivate();
};

#endif // __RENOTIFIER_H__
