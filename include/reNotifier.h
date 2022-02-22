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
#include "esp_timer.h"
#include "project_config.h"
#include "def_consts.h"
#include "reEsp32.h"

typedef enum {
  FNS_OK = 0,
  FNS_SLOWDOWN,
  FNS_FAILURE,
  FNS_LOCKED,
} notify_state_t;

typedef enum {
  FNK_NONE = 0,      // No notification
  FNK_FAILURE,       // Only in case of service failure
  FNK_RECOVERY,      // Only when the service is restored, without considering the previous failure notification
  FNK_AUTO,          // When restoring the service, if there was a previous failure notification
  FNK_FORCED         // In both cases
} notify_kind_t;

class reFailureNotifier;

typedef bool (*cb_send_notify_t) (reFailureNotifier* notifier, bool notify_alert, const char* object, notify_state_t state, int32_t value, time_t time_failure, time_t time_state);

class reFailureNotifier {
  public:
    reFailureNotifier(const char* object, bool notify_alert, notify_kind_t kind, time_t* delay_sec, cb_send_notify_t cb_notify);
    ~reFailureNotifier();

    bool sendExNotify(notify_state_t state, time_t time_state, int32_t ext_value, char* ext_object);
    void setState(notify_state_t new_state, time_t time_state, char* ext_object);
    
    void lock();
    void unlock();
    bool locked();

    void timerTimeout();
  private:
    notify_kind_t _kind = FNK_NONE;
    notify_state_t _state = FNS_OK;
    const char* _object = nullptr;    
    char* _ext_object = nullptr;
    time_t _time_failure = 0;
    time_t* _notify_delay = nullptr;
    bool _notify_alert = false;
    bool _notify_send = false;
    cb_send_notify_t _notify_cb = nullptr;
    esp_timer_handle_t _timer = nullptr;

    bool sendNotify(notify_state_t state, time_t time_state, int32_t value);
    bool timerStart();
    bool timerStop();
};

#endif // __RENOTIFIER_H__
