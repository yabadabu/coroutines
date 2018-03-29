#pragma once

namespace Coroutines {

  // -------------------------------------------
  namespace internal {

    template< typename T >
    struct ifCanReadFromChannel {
      TTypedChannel<T>             channel = 0;
      T                            obj;                 // Temporary storage to hold the recv data
      std::function<void(T& obj)>  cb;
      ifCanReadFromChannel(TTypedChannel<T> new_channel, std::function< void(T) >&& new_cb)
        : channel(new_channel)
        , cb(new_cb)
      { }
      void declareEvent(TWatchedEvent* we) {
        *we = canRead(channel);
      }
      bool run() {
        if (obj << channel) {
          cb(obj);
          return true;
        }
        return false;
      }
    };

  }

  // Helper function to deduce the arguments in a fn, not as the ctor args
  template< typename T, typename TFn >
  internal::ifCanReadFromChannel<T> ifCanRead(TTypedChannel<T> chan, TFn&& new_cb) {
    return internal::ifCanReadFromChannel<T>(chan, new_cb);
  }

  // -------------------------------------------------------------
  struct ifTimeout {
    TTimeDelta                   delta;
    std::function<void(void)>    cb;
    TWatchedEvent                saved_we;
    ifTimeout(TTimeDelta new_delta, std::function< void() >&& new_cb)
      : delta(new_delta)
      , cb(new_cb)
      , saved_we(delta)
    { }
    void declareEvent(TWatchedEvent* we) {
      *we = saved_we;
    }
    bool run() {
      cb();
      return true;
    }
  };

  // -------------------------------------------------------------
  // Check if we can read from a socket
  namespace internal {
    struct ifCanReadFromSocket {
      Net::TSocket                      sock;
      std::function<void(Net::TSocket)> cb;
      ifCanReadFromSocket(Net::TSocket new_sock, std::function< void(Net::TSocket) >&& new_cb)
        : sock(new_sock)
        , cb(new_cb)
      { }
      void declareEvent(TWatchedEvent* we) {
        *we = canRead(sock);
      }
      bool run() {
        cb(sock);
        return true;
      }
    };
  }

  // Helper function 
  template< typename TFn >
  internal::ifCanReadFromSocket ifCanRead(Net::TSocket sock, TFn&& new_cb) {
    return internal::ifCanReadFromSocket(sock, new_cb);
  }

  // Check if a timer channel generates an event
  struct ifTimer {
    TTimeHandle                     handle;
    std::function<void(TTimeStamp)> cb;
    ifTimer(TTimeHandle new_handle, std::function< void(TTimeStamp ts) >&& new_cb)
      : handle(new_handle)
      , cb(new_cb)
    { }
    void declareEvent(TWatchedEvent* we) {
      *we = handle.timeForNextEvent();
    }
    bool run() {
      TTimeStamp ts;
      if (ts << handle) {
        cb(ts);
        return true;
      }
      return false;
    }
  };

  // -----------------------------------------------------------------------------
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
    bool runOption(int idx, int the_option);

    template< typename A, typename ...Args >
    bool runOption(int idx, int the_option, A& a, Args... args) {
      return
        (idx == the_option)
        ? a.run()
        : runOption(idx + 1, the_option, args...);
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
    if (n >= 0 && n < nwe) {
      // If could not read the channel which fired up... return -1
      if (!internal::runOption(0, n, args...))
        return -1;
    }

    // Return the index
    return n;
  }

}
