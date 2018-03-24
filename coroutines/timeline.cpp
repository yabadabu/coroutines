#include "timeline.h"
#include "coroutines.h"

#ifdef _WIN32
  // Taken from redis sources ae.c
  #include <sys/types.h> 
  #include <time.h>
  int gettimeofday(struct timeval * tp, struct timezone * tzp) {
    // Note: some broken versions only have 8 trailing zero's, the correct epoch has 9 trailing zero's
    // This magic number is the number of 100 nanosecond intervals since January 1, 1601 (UTC)
    // until 00:00:00 January 1, 1970 
    static const uint64_t EPOCH = ((uint64_t)116444736000000000ULL);

    SYSTEMTIME  system_time;
    FILETIME    file_time;
    uint64_t    time;

    GetSystemTime(&system_time);
    SystemTimeToFileTime(&system_time, &file_time);
    time = ((uint64_t)file_time.dwLowDateTime);
    time += ((uint64_t)file_time.dwHighDateTime) << 32;

    tp->tv_sec = (long)((time - EPOCH) / 10000000L);
    tp->tv_usec = (long)(system_time.wMilliseconds * 1000);
    return 0;
  }

#else
  #include <sys/time.h>
  #include <unistd.h>
#endif

namespace Coroutines {
  
  void wakeUp(TWatchedEvent* we);

  TTimeStamp current_timestamp;
  TList      waiting_for_timeouts;

  void getSecondsAndMilliseconds(TTimeDelta ts, long* num_secs, long* num_millisecs) {
    assert(num_secs && num_millisecs);
    *num_secs = (long) (ts / 1000);
    *num_millisecs = ts % 1000;
  }

  // Using millisecond resolution
  TTimeStamp now() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000 + (tv.tv_usec / 1000);
  }

  void checkTimeoutEvents() {
    current_timestamp = now();
    auto we = static_cast<TWatchedEvent*>( waiting_for_timeouts.first );
    while (we) {
      assert(we->event_type == EVT_TIMEOUT);
      if (we->time.time_to_trigger <= current_timestamp)
        wakeUp( we );
      we = static_cast<TWatchedEvent*>(we->next);
    }
  }

  void registerTimeoutEvent(TWatchedEvent* we) {
    assert(we->event_type == EVT_TIMEOUT);
    waiting_for_timeouts.append(we);
  }

  void unregisterTimeoutEvent(TWatchedEvent* we) {
    assert(we->event_type == EVT_TIMEOUT);
    waiting_for_timeouts.detach(we);
  }

  // -------------------------------------------------
  namespace Time {
    
    void sleep(TTimeDelta ms_to_sleep) {
      wait(nullptr, 0, ms_to_sleep);
    }

    TWatchedEvent after(TTimeDelta ms_to_sleep) {
      TWatchedEvent we(ms_to_sleep);
      return we;
    }
  }

  namespace detail {
    // Recursive event terminator
    void fillChoose(TWatchedEvent* we) { 
    }
    
    void runOption(int idx, int the_option) {
      assert(false && "Something weird is going on...\n");
    }
  }

}
