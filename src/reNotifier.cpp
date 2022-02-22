#include "reNotifier.h"
#include "reEsp32.h"
#include "rLog.h"
#include <string.h>

static const char* logTAG = "NOTIFIER";

reFailureNotifier::reFailureNotifier(const char* object, bool notify_alert, notify_kind_t kind, time_t* delay_sec, cb_send_notify_t cb_notify)
{
  _notify_alert = notify_alert;
  _object = object;
  _ext_object = nullptr;
  _kind = kind;
  _notify_delay = delay_sec;
  _notify_cb = cb_notify;

  _state = FNS_OK;
  _time_failure = 0;
  _notify_send = false;
  _timer = nullptr;
}

reFailureNotifier::~reFailureNotifier() 
{
  timerStop();
  if (_ext_object) {
    free(_ext_object);
    _ext_object = nullptr;
  };
}

bool reFailureNotifier::sendNotify(notify_state_t state, time_t time_state, int32_t value)
{
  rlog_i(logTAG, "Send notification for [ %s ] with state %d, value %d", _object, _state, value);
  if (_notify_cb) {
    if (_ext_object) {
      return _notify_cb(this, _notify_alert, (const char*)_ext_object, state, value, _time_failure, time_state);
    } else {
      return _notify_cb(this, _notify_alert, _object, state, value, _time_failure, time_state);
    };
  };
  return false;
}

bool reFailureNotifier::sendExNotify(notify_state_t state, time_t time_state, int32_t ext_value, char* ext_object)
{
  if (ext_object) {
    if (_ext_object) free(_ext_object);
    _ext_object = ext_object;
  };
  if (_state != FNS_LOCKED) {
    return sendNotify(state, time_state, ext_value);
  };
  return false;
}

void reFailureNotifier::setState(notify_state_t new_state, time_t time_state, char* ext_object)
{
  if (ext_object) {
    if (_ext_object) free(_ext_object);
    _ext_object = ext_object;
  };
  if ((_state != FNS_LOCKED) && (new_state != _state)) {
    if (new_state == FNS_OK) {
      _state = new_state;
      // Stop the delayed send timer
      timerStop();
      // Send only if the failure time has exceeded the specified interval
      time_t duration_failure = time_state - _time_failure;
      if ((_time_failure > 0)
      && ((_kind == FNK_RECOVERY) || ((_kind == FNK_AUTO) && _notify_send) || (_kind == FNK_FORCED))
      && (!(_notify_delay) || (duration_failure >= *_notify_delay))) {
        sendNotify(_state, time_state, 0);
      };
      // Reset flags
      _time_failure = 0;
      _notify_send = false;
    } else {
      // If the new status is slow and the current one is an error, then ignore until the status is good
      if (!(new_state == FNS_SLOWDOWN && _state == FNS_FAILURE)) {
        _state = new_state;
        if (_time_failure == 0) {
          if (time_state == 0) {
            _time_failure = time(nullptr);
          } else {
            _time_failure = time_state;
          };
        };
        if ((_kind == FNK_FAILURE) || (_kind == FNK_AUTO) || (_kind == FNK_FORCED)) {
          // Wait before notification so you don't spam short crashes
          time_t dalay_failure = time(nullptr) - _time_failure;
          if ((!(_notify_delay) || (dalay_failure >= *_notify_delay)) || !timerStart()) {
            _notify_send = sendNotify(_state, time_state, 0);
          };
        };
      };
    };
  };
}

// -----------------------------------------------------------------------------------------------------------------------
// -------------------------------------------------- Temporary blocking -------------------------------------------------
// -----------------------------------------------------------------------------------------------------------------------

void reFailureNotifier::lock()
{
  if (_state != FNS_LOCKED) {
    timerStop();
    _time_failure = 0;
    _notify_send = false;
    _state = FNS_LOCKED;
  };
}

void reFailureNotifier::unlock()
{
  if (_state == FNS_LOCKED) {
    timerStop();
    _time_failure = 0;
    _notify_send = false;
    _state = FNS_OK;
  };
}

bool reFailureNotifier::locked()
{
  return (_state == FNS_LOCKED);
}

// -----------------------------------------------------------------------------------------------------------------------
// ------------------------------------------------ Delayed notifications ------------------------------------------------
// -----------------------------------------------------------------------------------------------------------------------

static void reFailureNotifierDelayTimeout(void* arg)
{
  if (arg) {
    reFailureNotifier* caller = (reFailureNotifier*)arg;
    caller->timerTimeout();
  };
}

void reFailureNotifier::timerTimeout()
{
  timerStop();
  if (!_notify_send && (_state != FNS_OK)) {
    _notify_send = sendNotify(_state, _time_failure, 0);
  };
}

bool reFailureNotifier::timerStart()
{
  if ((_notify_delay) && (*_notify_delay > 0)) {
    if (_timer == nullptr) {
      esp_timer_create_args_t timer_cfg;
      memset(&timer_cfg, 0, sizeof(esp_timer_create_args_t));
      timer_cfg.name = _object;
      timer_cfg.callback = reFailureNotifierDelayTimeout;
      timer_cfg.arg = this;
      RE_OK_CHECK(logTAG, esp_timer_create(&timer_cfg, &_timer), return false);
    } else {
      if (esp_timer_is_active(_timer)) {
        RE_OK_CHECK(logTAG, esp_timer_stop(_timer), return false);
      };
    };
    if (_timer != nullptr) {
      rlog_d(logTAG, "Start failure notification timer for [ %s ]", _object);
      RE_OK_CHECK(logTAG, esp_timer_start_once(_timer, *_notify_delay * 1000000), return false);
    };
    return true;
  };
  return false;
}

bool reFailureNotifier::timerStop()
{
  if (_timer != nullptr) {
    if (esp_timer_is_active(_timer)) {
      RE_OK_CHECK(logTAG, esp_timer_stop(_timer), return false);
    };
    RE_OK_CHECK(logTAG, esp_timer_delete(_timer), return false);
    _timer = nullptr;
  };
  rlog_d(logTAG, "Deleted failure notification timer for [ %s ]", _object);
  return true;
}


