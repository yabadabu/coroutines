#pragma once

namespace Coroutines {

  // -------------------------------------------
  namespace internal {

    template< typename T >
    struct ifCanPullDef {
      TTypedChannel<T>             channel = 0;
      T                            obj;                 // Temporary storage to hold the recv data
      std::function<void(T& obj)>  cb;
      ifCanPullDef(TTypedChannel<T> new_channel, std::function< void(T) >&& new_cb)
        : channel(new_channel)
        , cb(new_cb)
      { }
      void declareEvent(TWatchedEvent* we) {
        *we = TWatchedEvent(channel.asU32(), eEventType::EVT_CHANNEL_CAN_PULL);
      }
      void run() {
        dbg("choose.can pull fired from channel c:%08x\n", channel);
        if (pull(channel, obj))
          cb(obj);
      }
    };

  }

  // Helper function to deduce the arguments in a fn, not as the ctor args
  template< typename T, typename TFn >
  internal::ifCanPullDef<T> ifCanPull(TTypedChannel<T> chan, TFn&& new_cb) {
    return internal::ifCanPullDef<T>(chan, new_cb);
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

  // Hide these templates from the user inside a internal namespace
  namespace internal {

    // --------------------------------------------
    // Recursive event terminator
    void fillChoose(TWatchedEvent* we);

    template< typename A, typename ...Args >
    void fillChoose(TWatchedEvent* we, A& a, Args... args) {
      a.declareEvent(we);
      fillChoose(we + 1, args...);
    }

    // --------------------------------------------
    void runOption(int idx, int the_option);

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
    internal::fillChoose(wes, args...);

    // Now call our std wait function which will return -1 or the index
    // of the entry which is ready
    int n = wait(wes, nwe);

    // Activate that option
    if (n >= 0 && n < nwe)
      internal::runOption(0, n, args...);

    // Return the index
    return n;
  }

}
