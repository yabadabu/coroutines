#ifndef INC_COROUTINE_EVENTS_H_
#define INC_COROUTINE_EVENTS_H_

namespace Coroutines {

  typedef uint32_t TEventID;

  TEventID createEvent( bool initial_value = false, const char* debug_name = nullptr );
  bool setEvent(TEventID evt);
  bool clearEvent(TEventID evt);
  bool isEventSet(TEventID evt);
  bool destroyEvent(TEventID evt);
  bool isValidEvent(TEventID evt);

}



#endif

