#ifndef INC_COROUTINES_WAIT_H_
#define INC_COROUTINES_WAIT_H_

namespace Coroutines {
  
  enum eEventType {
    EVT_USER_EVENT = 0
  , EVT_COROUTINE_ENDS
  , EVT_TIMEOUT
  , EVT_CHANNEL_CAN_PUSH
  , EVT_CHANNEL_CAN_PULL
  , EVT_SOCKET_IO_CAN_READ
  , EVT_SOCKET_IO_CAN_WRITE
  , EVT_INVALID
  , EVT_TYPES_COUNT
  };

  // --------------------------
  struct TWatchedEvent : public TListItem {
    THandle        owner;         // maps to current()
    eEventType     event_type;    // Set by the ctor

    union {

      struct {
        TChannel*  channel;
        void*      data_addr;
        size_t     data_size;
      } channel;

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

    };

    // Specialized ctors
    TWatchedEvent() : event_type(EVT_INVALID) { }

    // Wait until the we can push/pull an item into/from that channel
    template< class TObj >
    TWatchedEvent(TChannel* new_channel, const TObj &obj, eEventType evt)
    {
      channel.channel = new_channel;
      channel.data_addr = (TObj*)&obj;
      channel.data_size = sizeof(TObj);
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

    TWatchedEvent(SOCKET_ID fd, eEventType evt) {
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

  // Wait a user provided function.
  void wait(TWaitConditionFn fn);

  // Wait for another coroutine to finish
  // wait while h is a coroutine handle
  void wait(THandle h);

  void waitIOEvent();
  void notifyIOEvent(THandle h);

  // We want to wait all the items in a range... do it
  template< typename iterator >
  void waitAll(iterator beg, iterator end) {
    while (beg != end) {
      wait(*beg);
      ++beg;
    }
  }

  // Wait until all coroutines have finished
  void waitAll(std::initializer_list<THandle> handles);

}

#endif
