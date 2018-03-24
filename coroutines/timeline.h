#ifndef INC_COROUTINES_TIMELINE_H_
#define INC_COROUTINES_TIMELINE_H_

#include <cstdint>
#include "list.h"

namespace Coroutines {

  // Num of milliseconds
  typedef uint64_t      TTimeStamp;
  typedef uint64_t      TTimeDelta;

  struct TWatchedEvent;

  void getSecondsAndMilliseconds(TTimeStamp ts, long* num_secs, long* num_millisecs);

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
    TTimeDelta milliseconds(int num_ms);
    TTimeDelta seconds(int num_secs);
    TWatchedEvent after(TTimeDelta ms_to_sleep);
    
    static const TTimeDelta Second = TTimeDelta(1000);
    static const TTimeDelta MilliSecond = TTimeDelta(1);


  }


}

#endif

