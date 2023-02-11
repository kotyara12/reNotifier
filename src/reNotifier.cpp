#include "reNotifier.h"
#include "reEsp32.h"
#include "rLog.h"
#include <string.h>

static const char* logTAG = "NOTIFIER";

#define HM_LOCKED       BIT0  // Blocking notifications for an external reason
#define HM_SENDED       BIT1  // Failure notification has been sent

reHealthMonitor::reHealthMonitor(const char* service, hm_notify_mode_t notify_mode, 
  msg_options_t msg_options, const char* msg_ok, const char* msg_failure, 
  uint8_t failure_thresold, hm_send_notify_t cb_notify)
{
  _service = service;
  _mode = notify_mode;
  _msg_options = msg_options;
  _msg_ok = msg_ok;
  _msg_failure = msg_failure;
  _fail_thresold = failure_thresold;
  _notify_cb = cb_notify;
  
  _state = ESP_OK;
  _fail_count = 0;
  _time_state = 0;
  _time_failure = 0;
  _notify_flags = 0;
  
  _object = nullptr;
  _notify_timer = nullptr;
  _notify_delay = nullptr;
  _notify_enable = nullptr;
}

reHealthMonitor::~reHealthMonitor() 
{
  timerStop();
  if (_object) {
    free(_object);
    _object = nullptr;
  };
}

void reHealthMonitor::assignParams(uint32_t* failure_confirm_timeout, uint8_t* enable_notify)
{
  _notify_delay = failure_confirm_timeout;
  _notify_enable = enable_notify;
}

bool reHealthMonitor::sendNotifyPrivate()
{
  if ((_notify_enable == nullptr) || (*_notify_enable != 0)) {
    const char* _msg_template = _state == ESP_OK ? _msg_ok : _msg_failure;
    if (_msg_template != nullptr) {
      const char* _msg_object = _object == nullptr ? _service : (const char*)_object;
      if (!(_notify_flags & HM_LOCKED)) {
        if (_notify_cb) {
          hm_notify_data_t data = {
            .monitor = this,
            .object = _msg_object,
            .msg_template = _msg_template,
            .msg_options = _msg_options,
            .state = _state,
            .time_state = _time_state,
            .time_failure = _time_failure
          };
          return _notify_cb(&data);
        };
      };
    };
  };
  return false;
}

void reHealthMonitor::setStateCustom(esp_err_t new_state, time_t time_state, bool forced_send, char* ext_object)
{
  // Replace pointer to external object if present
  if (ext_object) {
    if (_object) free(_object);
    _object = ext_object;
  };
  // Process new state
  if (!(_notify_flags & HM_LOCKED)) {
    // CHANGE :: FAILURE -> OK
    if (new_state == ESP_OK) {
      if (_state != new_state) {
        _state = new_state;
        _time_state = time_state > 0 ? time_state : time(nullptr);
        // Stop the delayed send timer, if exists
        timerStop();
        // Send only if the failure time has exceeded the specified interval
        time_t duration_failure = _time_state - _time_failure;
        if ((_time_failure > 0)
          && ((_mode == HM_RECOVERY) || ((_mode == HM_AUTO) && (_notify_flags & HM_SENDED)) || (_mode == HM_FORCED))
          && ((_notify_delay == nullptr) || (duration_failure >= *_notify_delay))) {
            sendNotifyPrivate();
        };
        // Reset internal counters
        _notify_flags &= ~HM_SENDED;
        _time_failure = 0;
        _fail_count = 0;
      };
    // CHANGE :: OK -> FAILURE
    } else {
      _state = new_state;
      _time_state = time_state > 0 ? time_state : time(nullptr);
      if (_time_failure == 0) _time_failure = _time_state;
      if (_fail_count < UINT8_MAX) _fail_count++;
      // If the notification has not yet been sent...
      if ((_fail_count >= _fail_thresold) 
        && ((_mode == HM_FAILURE) || (_mode == HM_AUTO) || (_mode == HM_FORCED))
        && ((_notify_flags & HM_SENDED) == 0)
        && !((_notify_timer != nullptr) && esp_timer_is_active(_notify_timer))) {
          time_t delay_failure = _time_state - _time_failure;
          // If the delay is not set, or more time has already passed, we send a notification immediately
          if (forced_send || (_notify_delay == nullptr) || (delay_failure >= *_notify_delay)) {
            if (sendNotifyPrivate()) {
              _notify_flags |= HM_SENDED;
            };
          // ...otherwise start the delay timer (which can be interrupted if the status returns to ESP_OK)
          } else {
            timerStart();
          };
      };
    };
  };
}

void reHealthMonitor::setState(esp_err_t new_state, time_t time_state)
{
  setStateCustom(new_state, time_state, false, nullptr);
}

void reHealthMonitor::forcedTimeout()
{
  if ((_notify_timer != nullptr) && esp_timer_is_active(_notify_timer)) {
    timerTimeout();
  };
}

// -----------------------------------------------------------------------------------------------------------------------
// -------------------------------------------------- Temporary blocking -------------------------------------------------
// -----------------------------------------------------------------------------------------------------------------------

void reHealthMonitor::lock()
{
  if (!(_notify_flags & HM_LOCKED)) {
    timerStop();
    _notify_flags |= HM_LOCKED;
    _notify_flags &= ~HM_SENDED;
    _time_failure = 0;
    _fail_count = 0;
    _state = ESP_OK;
  };
}

void reHealthMonitor::unlock()
{
  if (_notify_flags & HM_LOCKED) {
    _notify_flags &= ~HM_LOCKED;
    _notify_flags &= ~HM_SENDED;
    _time_failure = 0;
    _fail_count = 0;
    _state = ESP_OK;
  };
}

bool reHealthMonitor::isLocked()
{
  return (_notify_flags & HM_LOCKED);
}

// -----------------------------------------------------------------------------------------------------------------------
// ------------------------------------------------ Delayed notifications ------------------------------------------------
// -----------------------------------------------------------------------------------------------------------------------

static void reHealthMonitorDelayTimeout(void* arg)
{
  if (arg) {
    reHealthMonitor* caller = (reHealthMonitor*)arg;
    caller->timerTimeout();
  };
}

void reHealthMonitor::timerTimeout()
{
  timerStop();
  if (!((_notify_flags & HM_SENDED) && (_state == ESP_OK))) {
    if (sendNotifyPrivate()) {
      _notify_flags |= HM_SENDED;
    };
  };
}

bool reHealthMonitor::timerStart()
{
  if ((_notify_delay != nullptr) && (*_notify_delay > 0)) {
    if (_notify_timer == nullptr) {
      esp_timer_create_args_t timer_cfg;
      memset(&timer_cfg, 0, sizeof(esp_timer_create_args_t));
      timer_cfg.name = "health_mon";
      timer_cfg.callback = reHealthMonitorDelayTimeout;
      timer_cfg.arg = this;
      RE_OK_CHECK(esp_timer_create(&timer_cfg, &_notify_timer), return false);
    } else {
      if (esp_timer_is_active(_notify_timer)) {
        return true;
      };
    };
    if (_notify_timer != nullptr) {
      RE_OK_CHECK(esp_timer_start_once(_notify_timer, *_notify_delay * 1000000), return false);
    };
    return true;
  };
  return false;
}

bool reHealthMonitor::timerStop()
{
  if (_notify_timer != nullptr) {
    if (esp_timer_is_active(_notify_timer)) {
      RE_OK_CHECK(esp_timer_stop(_notify_timer), return false);
    };
    RE_OK_CHECK(esp_timer_delete(_notify_timer), return false);
    _notify_timer = nullptr;
  };
  return true;
}


