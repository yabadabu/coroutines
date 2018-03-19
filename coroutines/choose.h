#pragma once

namespace Coroutines {

  namespace Time {

    void sleep(TTimeDelta ms_to_sleep) {
      wait(nullptr, 0, ms_to_sleep);
    }

    TTimeDelta milliseconds(int num_ms) {
      return TTimeDelta(num_ms);
    }

    TWatchedEvent after(TTimeDelta ms_to_sleep) {
      TWatchedEvent we(ms_to_sleep);
      return we;
    }

  }

}


namespace Coroutines {

  // -------------------------------------------
  template< typename T >
  struct ifCanPullDef {
    TChannel<T>*                 channel = nullptr;
    T                            obj;                 // Temporary storage to hold the recv data
    std::function<void(T& obj)>  cb;
    ifCanPullDef(TChannel<T>* new_channel, std::function< void(T) >&& new_cb)
      : channel(new_channel)
      , cb(new_cb)
    { }
    void declareEvent(TWatchedEvent* we) {
      *we = TWatchedEvent(channel, obj, eEventType::EVT_CHANNEL_CAN_PULL);
    }
    void run() {
      if (channel->pull(obj))
        cb(obj);
    }
  };

  // Helper function to deduce the arguments in a fn, not as the ctor args
  template< typename T, typename TFn >
  ifCanPullDef<T> ifCanPull(TChannel<T>* chan, TFn&& new_cb) {
    return ifCanPullDef<T>(chan, new_cb);
  }

  // -------------------------------------------
  template< typename T >
  struct ifCanPushDef {
    TChannel<T>*                 channel = nullptr;
    T                            obj;                 // Temporary storage to hold the recv data
    std::function<void(T& obj)>  cb;
    ifCanPushDef(TChannel<T>* new_channel, std::function< void(T) >&& new_cb)
      : channel(new_channel)
      , cb(new_cb)
    { }
    void declareEvent(TWatchedEvent* we) {
      *we = TWatchedEvent(channel, obj, eEventType::EVT_CHANNEL_CAN_PUSH);
    }
    void run() {
      if (channel->push(obj))
        cb(obj);
    }
  };

  // Helper function to deduce the arguments in a fn, not as the ctor args
  template< typename T, typename TFn >
  ifCanPushDef<T> ifCanPush(TChannel<T>* chan, TFn&& new_cb) {
    return ifCanPushDef<T>(chan, new_cb);
  }

  // -------------------------------------------------------------
  struct ifTimeout {
    TTimeDelta                   delta;
    std::function<void(void)>    cb;
    ifTimeout(TTimeDelta new_delta, std::function< void() >&& new_cb)
      : delta(new_delta)
      , cb(new_cb)
    { }
    void declareEvent(TWatchedEvent* we) {
      *we = TWatchedEvent(Time::after(delta));
    }
    void run() {
      cb();
    }
  };

  // Hide these templates from the user inside a detail namespace
  namespace detail {

    // --------------------------------------------
    // Recursive event terminator
    void fillChoose(TWatchedEvent* we) { }

    template< typename A, typename ...Args >
    void fillChoose(TWatchedEvent* we, A& a, Args... args) {
      a.declareEvent(we);
      fillChoose(we + 1, args...);
    }

    // --------------------------------------------
    void runOption(int idx, int the_option) {
      assert( false && "Something weird is going on...\n");
    }

    template< typename A, typename ...Args >
    void runOption(int idx, int the_option, A& a, Args... args) {
      if (idx == the_option)
        a.run();
      else
        runOption(idx + 1, the_option, args...);
    }

  }

  // -----------------------------------------------------------------------------
  // Templatized fn to deal with multiple args
  // This gets called with a bunch of select cases. For each one
  // we only require to have a 'declareEvent' member and the 'run' method
  // (no virtuals were fired in this experiment)
  template< typename ...Args >
  int choose(Args... args) {

    // This will contain the number of arguments
    auto nwe = (int) sizeof...(args);

    // We need a linear continuous array of watched events objs
    TWatchedEvent wes[sizeof...(args)];

    // update our array. Each argument will fill one slot of the array
    // each arg provides one we.
    detail::fillChoose(wes, args...);

    // Now call our std wait function which will return -1 or the index
    // of the entry which is ready
    int n = wait(wes, nwe);

    // Activate that option
    if (n >= 0 && n < nwe)
      detail::runOption(0, n, args...);

    // Return the index
    return n;
  }

}
