#include "coroutines.h"

namespace Coroutines {

  // -------------------------------------------------------------
  namespace internal {

    // -------------------------------------------------------------
    // Container of all real channels objectss
    std::vector<TBaseChan*> all_channels;

    // -------------------------------------------------------------
    TChanHandle registerChannel(TBaseChan* c, TChanHandle::eClassID channel_type) {
      // Right now add it to the end...
      all_channels.push_back(c);
      c->handle = TChanHandle(channel_type, (int32_t)all_channels.size() - 1);
      return c->handle;
    }

    // -------------------------------------------------------------
    TBaseChan* TBaseChan::findChannelByHandle(TChanHandle h) {

      // The channel id is valid?
      if (h.index >= all_channels.size())
        return nullptr;

      auto c = all_channels[h.index];
      assert(c);

      return (c->handle == h) ? c : nullptr;
    }
  
    // ------------------------------------------------------------
    // For each elem pushed, wakeup one waiter to pull
    bool TBaseChan::detachOneWaitingForPull() {
      auto we = waiting_for_pull.detachFirst< TWatchedEvent >();
      if (we) {
        assert(we->channel.handle == handle);
        assert(we->event_type == EVT_CHANNEL_CAN_PULL);
        wakeUp(we);
        return true;
      }
      return false;
    }

    // ------------------------------------------------------------
    // For each elem pulled, wakeup one waiter to push
    bool TBaseChan::detachOneWaitingForPush() {
      auto we = waiting_for_push.detachFirst< TWatchedEvent >();
      if (we) {
        assert(we->channel.handle == handle);
        assert(we->event_type == EVT_CHANNEL_CAN_PUSH);
        wakeUp(we);
        return true;
      }
      return false;
    }

  }

  // -------------------------------------------------------------
  // -------------------------------------------------------------
  // -------------------------------------------------------------
  struct TTimeChan : public internal::TBaseChan {
    TTimeStamp next;
    TTimeDelta interval;
    bool       is_periodic = false;
    void prepareNext() {
      if (!is_periodic) {
        close();
        return;
      }
      // In case more than 1 interval has passed since last event
      auto n = 1 + (Time::now() - next) / interval;
      next = next + n * interval;
    }
    TTimeChan(TTimeDelta amount_of_time_between_events, bool new_is_periodic)
      : next(Time::now() + amount_of_time_between_events)
      , interval(amount_of_time_between_events)
      , is_periodic(new_is_periodic)
    { }

    bool pullTime(TTimeStamp& ts) {

      // Requesting use in a closed channel?
      if (closed())
        return false;

      TTimeStamp right_now = Time::now();

      // We arrive too late? The event has triggered?
      if (next < right_now) {
        prepareNext();
        return true;
      }

      TTimeDelta time_for_event = next - right_now;

      TWatchedEvent wes[2];
      wes[0] = TWatchedEvent(time_for_event);

      // We can also exit from the wait IF this channel 
      // becomes 'closed' while we are waiting.
      // The 'close' will trigger this event
      wes[1] = TWatchedEvent(handle, eEventType::EVT_CHANNEL_CAN_PULL);
      int idx = wait(wes, 2);
      if (idx == -1)
        return false;

      ts = Time::now();

      prepareNext();

      // Return true only if the timer was really triggered
      return (idx == 0);
    }
  };

  TTimeHandle every(TTimeDelta interval_time) {
    TTimeChan* c = new TTimeChan(interval_time, true);
    return internal::registerChannel(c, TChanHandle::eClassID::CT_TIMER);
  }

  TTimeHandle after(TTimeDelta interval_time) {
    TTimeChan* c = new TTimeChan(interval_time, false);
    return internal::registerChannel(c, TChanHandle::eClassID::CT_TIMER);
  }

  bool operator<<(TTimeStamp& value, TTimeHandle cid) {
    auto c = internal::TBaseChan::findChannelByHandle(cid);
    if (!c || c->closed())
      return false;
    assert(cid.class_id == TChanHandle::eClassID::CT_TIMER);
    TTimeChan* tc = (TTimeChan*)c;
    return tc->pullTime(value);
  }

  TTimeDelta TTimeHandle::timeForNextEvent() const {
    auto c = internal::TBaseChan::findChannelByHandle(*this);
    if (!c || c->closed())
      return TTimeDelta::zero();
    assert(class_id == TChanHandle::eClassID::CT_TIMER);
    TTimeChan* tc = (TTimeChan*) c;
    return tc->next - Time::now();
  }

  bool pull(TTimeHandle h) {
    TTimeStamp ts;
    return (ts << h);
  }

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

  bool isChannel(TChanHandle cid) {
    auto c = internal::TBaseChan::findChannelByHandle(cid);
    return c != nullptr;
  }

}
