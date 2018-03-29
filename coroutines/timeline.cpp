#include <chrono>
#include "timeline.h"
#include "coroutines.h"

namespace Coroutines {
  
  void wakeUp(TWatchedEvent* we);

  TTimeStamp current_timestamp;
  TList      waiting_for_timeouts;

  namespace Time {

    asStr::asStr(TTimeDelta dt) {
      auto num_ns = dt.count();
      int num_mins = asMinutes(dt) % 60;
      int num_secs = asSeconds(dt) % 60;
      long num_millis = long(num_ns % 1000000000) / 1000;
      snprintf(buf, sizeof(buf) - 1, "%02d:%02d:%06ld", num_mins, num_secs, num_millis);
    }

    int32_t asMicroSeconds(TTimeDelta t) {
      return (uint32_t) (std::chrono::duration_cast<std::chrono::microseconds>(t).count());
    }

    int32_t asMilliSeconds(TTimeDelta t) {
      return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(t).count();
    }

    int32_t asSeconds(TTimeDelta t) {
      return (uint32_t)std::chrono::duration_cast<std::chrono::seconds>(t).count();
    }

    int32_t asMinutes(TTimeDelta t) {
      return (uint32_t)std::chrono::duration_cast<std::chrono::minutes>(t).count();
    }

    TTimeStamp now() {
      return std::chrono::high_resolution_clock::now();
    }
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
