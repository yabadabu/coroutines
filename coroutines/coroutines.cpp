#include "coroutines.h"
#include "io_events.h"
#include "fcontext/fcontext.h"
#include <vector>

#pragma comment(lib, "fcontext.lib")

namespace Coroutines {

  static const uint16_t INVALID_ID = 0xffff;

  namespace internal {

    // -----------------------------------------
    THandle h_current;
    struct  TCoro;

    TIOEvents              io_events;
    std::vector< TCoro* >  coros;
    std::vector< THandle > coros_free;

    // -----------------------------------------
    struct TCoro {

      enum eState {
        UNINITIALIZED
      , RUNNING
      , WAITING_FOR_CONDITION
      , WAITING_FOR_EVENT
      , FREE
      };

      eState                    state = UNINITIALIZED;
      THandle                   this_handle;

      // Wait
      TWaitConditionFn          must_wait;
      TList                     waiting_for_me;
      TWatchedEvent*            event_waking_me_up = nullptr;      // Which event took us from the WAITING_FOR_EVENT

      // User entry point 
      TBootFn                   boot_fn = nullptr;
      void*                     boot_fn_arg = nullptr;

      // Low Level 
      fcontext_transfer_t       ip;
      fcontext_stack_t          stack;
      fcontext_transfer_t       caller_ctx;

      static void ctxEntryFn(fcontext_transfer_t t) {
        TCoro* co = reinterpret_cast<TCoro*>(t.data);
        assert(co);
        co->runMain(t);
      }

      void runUserFn() {
        assert(boot_fn);
        assert(boot_fn_arg);
        (*boot_fn)(boot_fn_arg);
      }

      static const size_t default_stack_size = 64 * 1024;
      void createStack() {
        stack = ::create_fcontext_stack(default_stack_size);
      }

      void destroyStack() {
        destroy_fcontext_stack(&stack);
      }

      // This has not yet started, it's only ready to run in the stack provided
      void resetIP() {
        ip.ctx = make_fcontext( stack.sptr, stack.ssize, &ctxEntryFn );
        ip.data = this;
      }
      
      void runMain(fcontext_transfer_t t) {
        caller_ctx = t;
        runUserFn();
        markAsFree();
        wakeOthersWaitingForMe();
        returnToCaller();
      }

      void markAsFree() {
        assert(state == RUNNING);
        // This will invalidate the current version of the handle
        this_handle.age++;
        coros_free.push_back(this_handle);
        // epilogue
        state = FREE;
      }

      void resume() {
        h_current = this_handle;
        ip = jump_fcontext(ip.ctx, ip.data);
      }

      void returnToCaller() {
        h_current = THandle();
        caller_ctx = jump_fcontext(caller_ctx.ctx, caller_ctx.data);
      }

      void wakeOthersWaitingForMe() {
        // Wake up those coroutines that were waiting for me to finish
        while (true) {
          auto we = waiting_for_me.detachFirst< TWatchedEvent >();
          if (!we)
            break;
          wakeUp(we);
        }
      }

    };

    // ----------------------------------------------------------
    TCoro* byHandle(THandle h) {

      // id must be in the valid range
      if (h.id >= coros.size())
        return nullptr;

      // if what we found matches the current age, we are a valid co
      TCoro* c = coros[h.id];
      assert(c->this_handle.id == h.id);
      if (h.age != c->this_handle.age)
        return nullptr;
      return c;
    }

    // ----------------------------------------------------------
    TCoro* findFree() {

      TCoro* co = nullptr;

      // The list is empty?, create a new co
      if (coros_free.empty()) {
        co = new TCoro;
        assert(co->state == TCoro::UNINITIALIZED);
        auto  idx = coros.size();
        co->this_handle.id = (uint16_t)idx;
        co->this_handle.age = 1;
        co->createStack();
        coros.push_back(co);
      }
      else {
        // Else, use one of the free list
        THandle h = coros_free.back();
        co = byHandle(h);
        assert(co);
        coros_free.pop_back();
        assert(co->state == TCoro::FREE);
        assert(coros[co->this_handle.id] == co);
      }

      co->state = TCoro::RUNNING;
      return co;
    }

    // --------------------------
    THandle start(TBootFn boot_fn, void* boot_fn_arg) {

      TCoro* co_new = findFree();
      assert(co_new);                               // Run out of free coroutines slots
      assert(co_new->state == TCoro::RUNNING);

      // Save arguments
      co_new->boot_fn = boot_fn;
      co_new->boot_fn_arg = boot_fn_arg;
      co_new->resetIP();

      // Can't start the new co from another co. Just register it
      // and the main thread will take care of starting it when possible
      if( !isHandle( current() ))
        co_new->resume();

      return co_new->this_handle;
    }

    // ----------------------------------------------------------
    void dump(const char* title) {
      //printf("Dump FirstFree: %d LastFree:%d FirstInUse:%d - LastInUse:%d %s\n", first_free, last_free, first_in_use, last_in_use, title);
      printf("Dump %s\n", title);
      int idx = 0;
      for (auto& co : coros) {
        printf("%04x : state:%d\n", idx, co->state);
        ++idx;
      }
    }

    // --------------------------------------------
    struct TScheduler {
      int nactives = 0;
      int runActives();
    };
    TScheduler           scheduler;
  }

  // --------------------------
  THandle current() {
    return internal::h_current;
  }

  // --------------------------------------------
  bool isHandle(THandle h) {
    return internal::byHandle(h) != nullptr;
  }

  // --------------------------------------------
  void yield() {
    assert(isHandle( current() ));
    auto co = internal::byHandle(current());
    assert(co);
    co->returnToCaller();
  }


  // -------------------------------
  // WAIT --------------------------
  // -------------------------------
  
  // --------------------------
  void wait(TWaitConditionFn fn) {
    // If the condition does not apply now, don't wait
    if (!fn())
      return;
    // We must be a valid co
    auto co = internal::byHandle(current());
    assert(co);
    co->state = internal::TCoro::WAITING_FOR_CONDITION;
    co->must_wait = fn;
    yield();
  }

  // Wait for another coroutine to finish
  // wait while h is a coroutine handle
  void wait(THandle h) {
    TWatchedEvent we(h);
    wait(&we, 1);
  }

  // Wait until all coroutines have finished
  void waitAll(std::initializer_list<THandle> handles) {
    waitAll(handles.begin(), handles.end());
  }

  // --------------------------------------------------------------
  int wait(TWatchedEvent* watched_events, int nwatched_events, TTimeDelta timeout) {
    assert(isHandle(current()));

    int n = nwatched_events;
    auto we = watched_events;

    // Check if any of the wait conditions are false, so there is no need to enter in the wait
    // for event mode
    int idx = 0;
    while (idx < n) {

      switch (we->event_type) {

      case EVT_CHANNEL_CAN_PULL:
        if (!we->channel.channel->empty() || we->channel.channel->closed())
          return idx;
        break;

      case EVT_CHANNEL_CAN_PUSH:
        if (!we->channel.channel->full() && !we->channel.channel->closed())
          return idx;
        break;

      case EVT_COROUTINE_ENDS: {
        if (!isHandle(we->coroutine.handle))
          return idx;
        break; }

      default:
        break;
      }

      ++idx;
    }

    // Attach to event watchers
    while (n--) {
      if (we->event_type == EVT_CHANNEL_CAN_PULL)
        we->channel.channel->waiting_for_pull.append(we);
      else
      if (we->event_type == EVT_CHANNEL_CAN_PUSH)
        we->channel.channel->waiting_for_push.append(we);
      else
      if (we->event_type == EVT_COROUTINE_ENDS) {
        // Check if the handle that we want to wait, still exists
        auto co_to_wait = internal::byHandle(we->coroutine.handle);
        if (co_to_wait)
          co_to_wait->waiting_for_me.append(we);
      }
      else if (we->event_type == EVT_SOCKET_IO_CAN_READ) {
        internal::io_events.add(we);
      }
      else if (we->event_type == EVT_SOCKET_IO_CAN_WRITE) {
        internal::io_events.add(we);
      }
      else {
        // Unsupported event type
        assert(false);
      }
      ++we;
    }

    // Do we have to install a timeout event watch?
    TWatchedEvent time_we;
    if (timeout != no_timeout) {
      assert(timeout >= 0);
      time_we = TWatchedEvent(timeout);
      registerTimeoutEvent(&time_we);
    }

    // Put ourselves to sleep
    auto co = internal::byHandle(current());
    assert(co);
    co->state = internal::TCoro::WAITING_FOR_EVENT;
    co->event_waking_me_up = nullptr;
    
    yield();

    // After we wakeup, we should be ready to go

    // There should be a reason to exit the waiting_for_event
    assert(co->event_waking_me_up != nullptr);
    int event_idx = 0;

    // If we had programmed a timeout, remove it
    if (timeout != no_timeout) {
      unregisterTimeoutEvent(&time_we);
      event_idx = wait_timedout;
    }

    // Detach from event watchers
    n = 0; ;
    we = watched_events;
    while (n <  nwatched_events) {
      if (we->event_type == EVT_CHANNEL_CAN_PULL)
        we->channel.channel->waiting_for_pull.detach(we);
      else if (we->event_type == EVT_CHANNEL_CAN_PUSH)
        we->channel.channel->waiting_for_push.detach(we);
      else 
      if (we->event_type == EVT_COROUTINE_ENDS) {
        // The coroutine we were waiting for is already gone, but 
        // we might be waiting for several co's to finish
        auto co_to_wait = internal::byHandle(we->coroutine.handle);
        if (co_to_wait)
          co_to_wait->waiting_for_me.detach(we);
      }
      else if (we->event_type == EVT_SOCKET_IO_CAN_READ) {
        internal::io_events.del(we);
      }
      else if (we->event_type == EVT_SOCKET_IO_CAN_WRITE) {
        internal::io_events.del(we);
      }
      else {
        // Unsupported event type
        assert(false);
      }
      if (co->event_waking_me_up == we)
        event_idx = n;
      ++we;
      ++n;
    }

    return event_idx;
  }

  // ---------------------------------------------------
  // Try to wake up all the coroutines which were waiting for the event
  void wakeUp(TWatchedEvent* we) {
    assert(we);
    auto co = internal::byHandle(we->owner);
    if (co) {
      co->event_waking_me_up = we;
      co->state = internal::TCoro::RUNNING;
    }
  }

  // ---------------------------------------------------
  void internal::TIOEvents::add(TWatchedEvent* we) {

    assert(we);
    assert(we->event_type == EVT_SOCKET_IO_CAN_READ || we->event_type == EVT_SOCKET_IO_CAN_WRITE);

    auto fd = we->io.fd;
    auto mode = we->event_type == EVT_SOCKET_IO_CAN_READ ? TO_READ : TO_WRITE;

    auto e = find(fd);

    assert(e && e->fd == fd);
    if (mode == TO_READ) {
      e->mask |= TO_READ;
      e->waiting_to_read.append(we);
      FD_SET(fd, &rfds);
    }
    else {
      e->mask |= TO_WRITE;
      e->waiting_to_write.append(we);
      FD_SET(fd, &wfds);
    }

    if (fd > max_fd)
      max_fd = fd;
  }

  void internal::TIOEvents::del(TWatchedEvent* we) {

    assert(we);
    assert(we->event_type == EVT_SOCKET_IO_CAN_READ || we->event_type == EVT_SOCKET_IO_CAN_WRITE);

    auto fd = we->io.fd;
    auto mode = we->event_type == EVT_SOCKET_IO_CAN_READ ? TO_READ : TO_WRITE;

    auto e = find(fd);

    if (!e || e->mask == 0)
      return;
    e->mask &= (~mode);

    assert(e && e->fd == fd);

    // Are we removing the largest fd we have?
    if (e->mask == 0 && e->fd == max_fd) {
      // Update max_fd when we remove the largest fd defined
      max_fd = 0;
      for (auto& e : entries) {
        if (e.fd > max_fd)
          max_fd = e.fd;
      }
    }
  }

  int internal::TIOEvents::update() {

    // Amount of time to wait
    timeval tm;
    tm.tv_sec = 0;
    tm.tv_usec = 0;

    fd_set fds_to_read, fds_to_write;

    memcpy(&fds_to_read, &rfds, sizeof(fd_set));
    memcpy(&fds_to_write, &wfds, sizeof(fd_set));

    // Do a real wait
    int num_events = 0;
    int retval = ::select(max_fd + 1, &fds_to_read, &fds_to_write, nullptr, &tm);
    if (retval > 0) {

      for (auto& e : entries) {

        if (!e.mask)
          continue;

        // we were waiting a read op, and we can read now...
        if ((e.mask & TO_READ) && FD_ISSET(e.fd, &fds_to_read)) {
          auto we = e.waiting_to_read.detachFirst< TWatchedEvent >();
          if (we) {
            assert(we->io.fd == e.fd);
            assert(we->event_type == EVT_SOCKET_IO_CAN_READ);
            wakeUp(we);
          }
        }

        if ((e.mask & TO_WRITE) && FD_ISSET(e.fd, &fds_to_write)) {
          auto we = e.waiting_to_write.detachFirst< TWatchedEvent >();
          if (we) {
            assert(we->io.fd == e.fd);
            assert(we->event_type == EVT_SOCKET_IO_CAN_WRITE);
            wakeUp(we);
          }
        }

      }
    }
    return num_events;
  }














  // ----------------------------------------------------------
  int executeActives() {
    
    internal::io_events.update();

    return internal::scheduler.runActives();
  }

  // --------------------------------------------
  int internal::TScheduler::runActives() {

    int nactives = 0;

    for (auto co : coros) {
      
      // Skip the free's
      if (co->state == TCoro::FREE)
        continue;

      if (co->state == TCoro::WAITING_FOR_EVENT) {
        ++nactives;
        continue;
      }

      // The 'waiting for condition' must be checked on each try/run
      if (co->state == TCoro::WAITING_FOR_CONDITION) {
        if (co->must_wait())
          continue;
        co->state = TCoro::RUNNING;
      }
      else {
        assert(co->state == TCoro::RUNNING);
      }
      
      co->resume();

      if (co->state == TCoro::RUNNING || co->state == TCoro::WAITING_FOR_CONDITION)
        ++nactives;

    }

    return nactives;
  }

}