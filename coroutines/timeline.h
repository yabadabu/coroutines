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

}

#endif

