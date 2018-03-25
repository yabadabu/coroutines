#ifndef INC_COROUTINES_WAIT_H_
#define INC_COROUTINES_WAIT_H_

namespace Coroutines {
  
  enum eEventType {
    EVT_USER_EVENT = 0
  , EVT_COROUTINE_ENDS
  , EVT_TIMEOUT
  , EVT_SOCKET_IO_CAN_READ
  , EVT_SOCKET_IO_CAN_WRITE
  , EVT_CHANNEL_CAN_PUSH
  , EVT_CHANNEL_CAN_PULL
  , EVT_INVALID
  , EVT_TYPES_COUNT
  };

  // --------------------------
  struct TWatchedEvent : public TListItem {
    THandle        owner;         // maps to current()
    eEventType     event_type;    // Set by the ctor

    union {

      struct {
        TTimeStamp time_programmed;    // Timestamp when it was programmed
        TTimeStamp time_to_trigger;    // Timestamp when will fire
      } time;

      struct {
        THandle    handle;
      } coroutine;

      struct {
        SOCKET_ID  fd;        // File descriptor
      } io;

      struct {
        TEventID   event_id;
      } user_event;

      struct {
        TChanHandle handle;
      } channel;

    };

    // Specialized ctors
    TWatchedEvent() : event_type(EVT_INVALID) { }

    // Wait until the we can push/pull an item into/from that channel
    TWatchedEvent(TChanHandle new_channel, eEventType evt)
    {
      assert( evt == EVT_CHANNEL_CAN_PUSH || evt == EVT_CHANNEL_CAN_PULL );
      channel.handle = new_channel;
      event_type = evt;
      owner = current();
    }

    // Wait until the coroutine has finished
    TWatchedEvent(THandle handle_to_wait)
    {
      coroutine.handle = handle_to_wait;
      event_type = EVT_COROUTINE_ENDS;
      owner = current();
    }

    TWatchedEvent(TTimeDelta timeout) {
      event_type = EVT_TIMEOUT;
      time.time_programmed = now();
      time.time_to_trigger = now() + timeout;
      owner = current();
    }

    TWatchedEvent(TEventID evt) {
      event_type = EVT_USER_EVENT;
      user_event.event_id = evt;
      owner = current();
    }

    TWatchedEvent(SOCKET_ID fd, eEventType evt) {
      assert(evt == EVT_SOCKET_IO_CAN_READ || evt == EVT_SOCKET_IO_CAN_WRITE);
      event_type = evt;
      io.fd = fd;
      owner = current();
    }

  };

  // WAIT_FOR_EVER means no timeout
  static const TTimeDelta no_timeout = ~((TTimeDelta)0);
  static const int wait_timedout = ~((int)0);
  
  // Will return the index of the event which wake up
  int wait(TWatchedEvent* watched_events, int nevents_to_watch, TTimeDelta timeout = no_timeout);

  void wait(TEventID evt);

  // Wait a user provided function.
  void wait(TWaitConditionFn fn);

  // Wait for another coroutine to finish
  // wait while h is a coroutine handle
  void wait(THandle h);

  // Empty fallback
  void waitAll();

  template< typename A, typename ...Args >
  void waitAll(A a, Args... args) {
    wait(a);
    waitAll(args...);
  }

}

#endif
