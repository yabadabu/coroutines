#ifndef INC_COROUTINES_TIMELINE_H_
#define INC_COROUTINES_TIMELINE_H_

#include <cstdint>
#include <chrono>
#include "list.h"

namespace Coroutines {

  // Currently stored in num of nanoseconds
  typedef std::chrono::high_resolution_clock::time_point   TTimeStamp;
  typedef std::chrono::duration<long long, std::nano>      TTimeDelta;

  // ---------------------------------------------------------
  namespace Time {

    TTimeStamp now();
    void sleep(TTimeDelta time_to_sleep);
    
    static const TTimeDelta MicroSecond = std::chrono::microseconds(1); // = std::chrono::microseconds(1);
    static const TTimeDelta MilliSecond = 1000 * MicroSecond;
    static const TTimeDelta Second = 1000 * MilliSecond;
    static const TTimeDelta Minute = 60 * Second;
    static const TTimeDelta Hour = 60 * Minute;
    
    // To convert internal nano secs to number (integer) of seconds
    // If TimeDelta t = 50.000.000 ns
    // asMicroSeconds(t) = 50000;
    // asMilliSeconds(t) = 50;
    int32_t asMicroSeconds(TTimeDelta t);
    int32_t asMilliSeconds(TTimeDelta t);
    int32_t asSeconds(TTimeDelta t);
    int32_t asMinutes(TTimeDelta t);

    struct asStr {
      char buf[32];
      asStr(TTimeDelta dt);
      const char* c_str() const { return buf; }
    };

  }

  struct TScopedTime {
    TTimeStamp      start;
    TScopedTime() : start(Time::now()) {
    }
    TTimeDelta elapsed() const { return Time::now() - start; }
  };

  // ------------------------------------------------------
  struct TWatchedEvent;

  namespace internal {

    void checkTimeoutEvents();
    void registerTimeoutEvent(TWatchedEvent* we);
    void unregisterTimeoutEvent(TWatchedEvent* we);

  }

}

#endif

