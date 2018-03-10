#include "coroutines.h"
#include "events.h"
#include <unordered_map>

namespace Coroutines {

  namespace internal {

    struct TEventData {
      TEventID     id = 0;
      bool         current_value = false;
      const char*  name = nullptr;
      TList        waiting_for_me;
    };

    std::unordered_map< TEventID, TEventData > all_events;
    TEventID next_event_id = 1;

    void attachToEvent(TEventID evt, TWatchedEvent* we) {
      assert(we);
      auto it = all_events.find(evt);
      if (it == all_events.end())
        return;
      it->second.waiting_for_me.append(we);
    }
    
    void detachFromEvent(TEventID evt, TWatchedEvent* we) {
      assert(we);
      auto it = all_events.find(evt);
      if (it == all_events.end())
        return;
      it->second.waiting_for_me.detach(we);
    }

  }

  using namespace internal;

  TEventID createEvent(bool initial_value, const char* debug_name ) {
    TEventData ed;
    ed.id = next_event_id++;
    ed.current_value = initial_value;
    ed.name = debug_name;
    all_events[ed.id] = ed;
    return ed.id;
  }

  bool setEvent(TEventID evt) {
    auto it = all_events.find(evt);
    if (it == all_events.end())
      return false;
    auto& ed = it->second;
    
    ed.current_value = true;

    // Wake everybody who was waiting the event to be set
    while (auto we = ed.waiting_for_me.detachFirst< TWatchedEvent >())
      wakeUp(we);

    return true;
  }

  bool clearEvent(TEventID evt) {
    auto it = all_events.find(evt);
    if (it == all_events.end())
      return false;
    it->second.current_value = false;
    return true;
  }

  bool isEventSet(TEventID evt) {
    auto it = all_events.find(evt);
    if (it == all_events.end())
      return false;
    return it->second.current_value;
  }

  bool destroyEvent(TEventID evt) {
    auto it = all_events.find(evt);
    if (it == all_events.end())
      return false;
    all_events.erase(it);
    return true;
  }

  bool isValidEvent(TEventID evt) {
    return all_events.find(evt) != all_events.end();
  }


}