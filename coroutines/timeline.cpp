#include <chrono>
#include "timeline.h"
#include "coroutines.h"

namespace Coroutines {
  
  void wakeUp(TWatchedEvent* we);

  TTimeStamp current_timestamp;
  TList      waiting_for_timeouts;

  void getSecondsAndMilliseconds(TTimeDelta ts, long* num_secs, long* num_millisecs) {
    assert(num_secs && num_millisecs);
    *num_secs = (long) (ts / Time::Second);
    *num_millisecs = ts % Time::Second;
  }

  // Using millisecond resolution
  TTimeStamp Time::now() {
    auto n = std::chrono::high_resolution_clock::now();
    auto usecs = std::chrono::duration_cast<std::chrono::microseconds>(n.time_since_epoch());
    return usecs.count();
  }

  namespace internal {

    void checkTimeoutEvents() {
      current_timestamp = Time::now();
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

  }

  // -------------------------------------------------
  namespace Time {
    
    void sleep(TTimeDelta time_to_sleep) {
      wait(nullptr, 0, time_to_sleep);
    }

  }

  namespace internal {
    // Recursive event terminator
    void fillChoose(TWatchedEvent* we) { 
    }
    
    bool runOption(int idx, int the_option) {
      assert(false && "Something weird is going on...\n");
      return false;
    }
  }

}
