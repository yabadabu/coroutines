#include "channel.h"
#include "coroutines.h"

namespace Coroutines {

  // -------------------------------------------------------------
  namespace internal {

    // -------------------------------------------------------------
    std::vector<TBaseChan*> all_channels;


    TChanHandle registerChannel(TBaseChan* c, eChannelType channel_type) {
      all_channels.push_back(c);
      c->handle = TChanHandle(channel_type, (int32_t)all_channels.size() - 1);
      return c->handle;
    }

    // -------------------------------------------------------------
    TBaseChan* TBaseChan::findChannelByHandle(TChanHandle h) {

      // The channel id is valid?
      if (h.index < 0 || h.index >= all_channels.size())
        return nullptr;

      auto c = all_channels[h.index];
      assert(c);

      return (c->handle == h) ? c : nullptr;
    }
  }

  // -------------------------------------------------------------
  // Specially for the time channels
  bool pull(TChanHandle cid) {
    auto c = internal::TBaseChan::findChannelByHandle(cid);
    if (!c || c->closed())
      return false;
    return c->pull(nullptr, 0);
  }

  // -------------------------------------------------------------
  // -------------------------------------------------------------
  // -------------------------------------------------------------
  class TTimeChan : public internal::TBaseChan {
    TTimeStamp next;
    TTimeDelta interval;
    bool       is_periodic = false;
    void prepareNext() {
      if (!is_periodic) {
        close();
        return;
      }
      // We could improve accuracy...
      next = now() + interval;
    }
  public:
    TTimeChan(TTimeDelta amount_of_time_between_events, bool new_is_periodic)
      : next(now() + amount_of_time_between_events)
      , interval(amount_of_time_between_events)
      , is_periodic(new_is_periodic)
    { }
    bool pull(void* obj, size_t nbytes) override {

      // Requesting use in a closed channel?
      if (closed())
        return false;

      TTimeStamp time_for_event = next - now();

      // We arrive too late? The event has triggered?
      if (time_for_event < 0) {
        prepareNext();
        return true;
      }

      TWatchedEvent wes[2];
      wes[0] = TWatchedEvent(time_for_event);

      // We can also exit from the wait IF this channel 
      // becomes 'closed' while we are waiting.
      // The 'close' will trigger this event
      wes[1] = TWatchedEvent(handle, eEventType::EVT_CHANNEL_CAN_PULL);
      int idx = wait(wes, 2);
      if (idx == -1)
        return false;

      if (obj) {
        assert(nbytes == sizeof(TTimeStamp));
        *(TTimeStamp*)obj = now();
      }

      prepareNext();

      // Return true only if the timer was really triggered
      return (idx == 0);
    }
  };

  TChanHandle every(TTimeDelta interval_time) {
    TTimeChan* c = new TTimeChan(interval_time, true);
    return internal::registerChannel(c, eChannelType::CT_TIMER);
  }

  TChanHandle after(TTimeDelta interval_time) {
    TTimeChan* c = new TTimeChan(interval_time, false);
    return internal::registerChannel(c, eChannelType::CT_TIMER);
  }

  bool operator<<(TTimeStamp& value, TChanHandle cid) {
    auto c = internal::TBaseChan::findChannelByHandle(cid);
    if (!c || c->closed())
      return false;
    return c->pull(&value, sizeof(value));
  }

  // -------------------------------------------------------
  // -------------------------------------------------------
  // -------------------------------------------------------
  //class TIOChan : public internal::TBaseChan {

  //};

  // -------------------------------------------------------
  // -------------------------------------------------------
  // -------------------------------------------------------
  bool close(TChanHandle cid) {
    auto c = internal::TBaseChan::findChannelByHandle(cid);
    if (!c || c->closed())
      return false;
    c->close();
    return true;
  }

}
