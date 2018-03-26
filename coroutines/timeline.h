#ifndef INC_COROUTINES_TIMELINE_H_
#define INC_COROUTINES_TIMELINE_H_

#include <cstdint>
#include "list.h"

namespace Coroutines {

  // Currently stored in num of microseconds
  typedef uint64_t      TTimeStamp;
  typedef  int64_t      TTimeDelta;

  struct TWatchedEvent;

  // ---------------------------------------------------------
  namespace Time {

    TTimeStamp now();
    void sleep(TTimeDelta ms_to_sleep);
    TWatchedEvent after(TTimeDelta ms_to_sleep);
    
    static const TTimeDelta MicroSecond = TTimeDelta(1);
    static const TTimeDelta MilliSecond = 1000 * MicroSecond;
    static const TTimeDelta Second = 1000 * MilliSecond;
    static const TTimeDelta Minute = 60 * Second;
    static const TTimeDelta Hour = 60 * Minute;

  }

  void getSecondsAndMilliseconds(TTimeDelta ts, long* num_secs, long* num_millisecs);

  void checkTimeoutEvents();
  void registerTimeoutEvent(TWatchedEvent* we);
  void unregisterTimeoutEvent(TWatchedEvent* we);

  struct TScopedTime {
    TTimeStamp      start;
    TScopedTime() : start(Time::now()) {
    }
    TTimeDelta elapsed() const { return Time::now() - start; }
  };


}

#endif

