#ifndef INC_COROUTINES_TIMELINE_H_
#define INC_COROUTINES_TIMELINE_H_

#include <cstdint>
#include "list.h"

namespace Coroutines {

  // Currently stored in num of milliseconds
  typedef uint64_t      TTimeStamp;
  typedef  int64_t      TTimeDelta;

  struct TWatchedEvent;

  void getSecondsAndMilliseconds(TTimeDelta ts, long* num_secs, long* num_millisecs);

  TTimeStamp now();
  void checkTimeoutEvents();
  void registerTimeoutEvent(TWatchedEvent* we);
  void unregisterTimeoutEvent(TWatchedEvent* we);

  struct TScopedTime {
    TTimeStamp      start;
    TScopedTime() : start(now()) {
    }
    TTimeDelta elapsed() const { return now() - start; }
  };

  // ---------------------------------------------------------
  namespace Time {

    void sleep(TTimeDelta ms_to_sleep);
    TWatchedEvent after(TTimeDelta ms_to_sleep);
    
    static const TTimeDelta Second = TTimeDelta(1000);
    static const TTimeDelta MilliSecond = TTimeDelta(1);


  }


}

#endif

