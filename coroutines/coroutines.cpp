#include "coroutines.h"
#include "io_events.h"
#include "fcontext/fcontext.h"
#include <vector>

#pragma comment(lib, "fcontext.lib")

namespace Coroutines {

  static const uint16_t INVALID_ID = 0xffff;

  // --------------------------
  namespace internal {

    // -----------------------------------------
    THandle h_current;
    struct  TCoro;

    // --------------------------------------------
    TIOEvents              io_events;
    std::vector< TCoro* >  coros;
    std::vector< THandle > coros_free;
    int runActives();
		size_t                 num_loops = 0;

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
      
      // Waiting events info
      TWatchedEvent             timeout_watched_event;
      TTimeStamp                timeout_watching_events = Coroutines::no_timeout;  // 
      TWatchedEvent*            watched_events = nullptr;
      int                       nwatched_events = 0;

      // User entry point 
      TBootFn                   boot_fn;

      // Low Level 
      fcontext_transfer_t       ip = { nullptr, nullptr };
      fcontext_stack_t          stack = { nullptr, 0 };
      fcontext_transfer_t       caller_ctx = { nullptr, nullptr };

      static void ctxEntryFn(fcontext_transfer_t t) {
        TCoro* co = reinterpret_cast<TCoro*>(t.data);
        assert(co);
        co->caller_ctx = t;
        co->runUserFn();
        co->epilogue();
      }

      void runUserFn() {
        assert(boot_fn);
        boot_fn();
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
      
      void epilogue() {
        markAsFree();
        wakeOthersWaitingForMe();
        returnToCaller();
      }

      void markAsFree() {
        assert(state == RUNNING);
        // This will invalidate the current version of the handle
        this_handle.age++;
        coros_free.push_back(this_handle);
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

    // Implemented in events.cpp
    void attachToEvent(TEventID evt, TWatchedEvent* we);
    void detachFromEvent(TEventID evt, TWatchedEvent* we);

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
    THandle start(TBootFn&& boot_fn) {

      TCoro* co_new = findFree();
      assert(co_new);                               // Run out of free coroutines slots
      assert(co_new->state == TCoro::RUNNING);

      // Save user entry point arguments
      co_new->boot_fn = boot_fn;

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
      printf("  Size of Co(%d)\n", (int)sizeof( TCoro ));
      int idx = 0;
      for (auto& co : coros) {
        printf("%04x : state:%d\n", idx, co->state);
        ++idx;
      }
    }

    // --------------------------------------------------------------
    void registerToEvents(TCoro* co, TWatchedEvent* watched_events, int nwatched_events, TTimeDelta timeout) {
      assert(co);

      int n = nwatched_events;
      auto we = watched_events;

      // Attach to event watchers
      while (n--) {
        switch (we->event_type) {

        case EVT_CHANNEL_CAN_PULL:
          we->channel.channel->waiting_for_pull.append(we);
          break;

        case EVT_CHANNEL_CAN_PUSH:
          we->channel.channel->waiting_for_push.append(we);
          break;

        case EVT_COROUTINE_ENDS: {
          // Check if the handle that we want to wait, still exists
          auto co_to_wait = internal::byHandle(we->coroutine.handle);
          if (co_to_wait)
            co_to_wait->waiting_for_me.append(we);
          break; }

        case EVT_SOCKET_IO_CAN_READ:
          internal::io_events.add(we);
          break;

        case EVT_SOCKET_IO_CAN_WRITE:
          internal::io_events.add(we);
          break;

        case EVT_USER_EVENT: {
          assert(we->user_event.event_id);
          internal::attachToEvent(we->user_event.event_id, we);
          break; }

        case EVT_TIMEOUT: {
          registerTimeoutEvent(we);
          break; }

        case EVT_NEW_CHANNEL_CAN_PULL: {
          auto c = TBaseChan::findChannelByHandle(we->nchannel.channel);
          assert(c);
          c->waiting_for_pull.append(we);
          break; }

        case EVT_NEW_CHANNEL_CAN_PUSH: {
          auto c = TBaseChan::findChannelByHandle(we->nchannel.channel);
          assert(c);
          c->waiting_for_push.append(we);
          break; }

        default:
          // Unsupported event type
          assert(false);
        }
        ++we;
      }

      // Do we have to install a timeout event watch?
      if (timeout != no_timeout) {
        assert(timeout >= 0);
        co->timeout_watched_event = TWatchedEvent(timeout);
        registerTimeoutEvent(&co->timeout_watched_event);
      }

      // Put ourselves to sleep
      co->timeout_watching_events = timeout;
      co->watched_events = watched_events;
      co->nwatched_events = nwatched_events;
      co->state = internal::TCoro::WAITING_FOR_EVENT;
      co->event_waking_me_up = nullptr;
    }

    // --------------------------------------------------------------
    int unregisterFromEvents(TCoro* co) {
      assert(co);

      int event_idx = 0;

      // If we had programmed a timeout, remove it
      if (co->timeout_watching_events != no_timeout) {
        unregisterTimeoutEvent(&co->timeout_watched_event);
        event_idx = wait_timedout;
      }

      // Detach from event watchers
      auto we = co->watched_events;
      int nwatched_events = co->nwatched_events;
      int n = 0;
      while (n <  nwatched_events) {
        switch (we->event_type) {

        case EVT_CHANNEL_CAN_PULL:
          we->channel.channel->waiting_for_pull.detach(we);
          break;

        case EVT_CHANNEL_CAN_PUSH:
          we->channel.channel->waiting_for_push.detach(we);
          break;

        case EVT_COROUTINE_ENDS: {
          // The coroutine we were waiting for is already gone, but
          // we might be waiting for several co's to finish
          auto co_to_wait = internal::byHandle(we->coroutine.handle);
          if (co_to_wait)
            co_to_wait->waiting_for_me.detach(we);
          break; }

        case EVT_SOCKET_IO_CAN_READ:
          internal::io_events.del(we);
          break;

        case EVT_SOCKET_IO_CAN_WRITE:
          internal::io_events.del(we);
          break;

        case EVT_USER_EVENT: {
          assert(we && we->user_event.event_id);
          internal::detachFromEvent(we->user_event.event_id, we);
          break; }

        case EVT_TIMEOUT:
          unregisterTimeoutEvent(we);
          break;

        case EVT_NEW_CHANNEL_CAN_PULL: {
          auto c = TBaseChan::findChannelByHandle(we->nchannel.channel);
          assert(c);
          c->waiting_for_pull.detach(we);
          break; }

        case EVT_NEW_CHANNEL_CAN_PUSH: {
          auto c = TBaseChan::findChannelByHandle(we->nchannel.channel);
          assert(c);
          c->waiting_for_push.detach(we);
          break; }

        default:
          // Unsupported event type
          assert(false);
        }

        if (co->event_waking_me_up == we)
          event_idx = n;
        ++we;
        ++n;
      }

      co->timeout_watching_events = no_timeout;

      // We are no longer waiting
      if(co->state == TCoro::WAITING_FOR_EVENT )
        co->state = internal::TCoro::RUNNING;

      return event_idx;
    }

  }

  // --------------------------
  THandle current() {
    return internal::h_current;
  }

  // --------------------------------------------
  bool isHandle(THandle h) {
    return internal::byHandle(h) != nullptr;
  }

  THandle start(TBootFn&& user_fn) {
    return internal::start(std::forward<TBootFn>(user_fn));
  }

	size_t getNumLoops() {
		return internal::num_loops;
	}

  // --------------------------------------------
  void yield() {
    assert(isHandle( current() ));
    auto co = internal::byHandle(current());
    assert(co);
    co->returnToCaller();
  }

  // --------------------------------------------
  void exitCo(THandle h) {
    auto co = internal::byHandle(h);
    if (!co)
      return;
    if (h == current()) {
      assert(isHandle(h));
      if (co)
        co->epilogue();
    }
    else {
      internal::unregisterFromEvents(co);
      co->markAsFree();
      co->wakeOthersWaitingForMe();
    }
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

  void wait(TEventID evt) {
    TWatchedEvent we(evt);
    wait(&we, 1);
  }

  // Wait for another coroutine to finish
  // wait while h is a coroutine handle
  void wait(THandle h) {
    TWatchedEvent we(h);
    wait(&we, 1);
  }

  void waitAll() {

  }
  
  // First check if any of the wait conditions we can quickly test are false, so there is no need to enter in the wait
  // for event mode
  int isAnyReadyWithoutBlocking(TWatchedEvent* watched_events, int nwatched_events) {
    int idx = 0;
    while (idx < nwatched_events) {
      auto we = watched_events + idx;

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

      case EVT_USER_EVENT: {
        assert(we && we->user_event.event_id);
        if (isEventSet(we->user_event.event_id))
          return idx;
        break; }

      case EVT_NEW_CHANNEL_CAN_PULL: {
        TBaseChan* c = TBaseChan::findChannelByHandle(we->nchannel.channel);
        if (c && (!c->empty() || c->closed()))
          return idx;
        break; }

      case EVT_NEW_CHANNEL_CAN_PUSH: {
        TBaseChan* c = TBaseChan::findChannelByHandle(we->nchannel.channel);
        if (c && (!c->full() && !c->closed()))
          return idx;
        break; }

      default:
        break;
      }

      ++idx;
    }

    return -1;
  }

  // --------------------------------------------------------------
  int wait(TWatchedEvent* watched_events, int nwatched_events, TTimeDelta timeout) {
    
    // Main thread can't wait for other co to finish
    assert(isHandle(current()));

    auto co = internal::byHandle(current());
    assert(co);

    if (int idx = isAnyReadyWithoutBlocking(watched_events, nwatched_events) >= 0)
      return idx;

    internal::registerToEvents(co, watched_events, nwatched_events, timeout);

    yield();

    // There should be a reason to exit the waiting_for_event
    assert(co->event_waking_me_up != nullptr);

    int event_idx = internal::unregisterFromEvents(co);
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

    FD_SET(fd, &err_fds);

    if (fd > max_fd)
      max_fd = fd;
  }

  // ---------------------------------------------------
  void internal::TIOEvents::del(TWatchedEvent* we) {

    assert(we);
    assert(we->event_type == EVT_SOCKET_IO_CAN_READ || we->event_type == EVT_SOCKET_IO_CAN_WRITE);

    auto fd = we->io.fd;
    auto mode = we->event_type == EVT_SOCKET_IO_CAN_READ ? TO_READ : TO_WRITE;

    auto e = find(fd);

    if (!e || e->mask == 0)
      return;

    assert(e && e->fd == fd);
    e->mask &= (~mode);

    if (mode == TO_READ) {
      FD_CLR(fd, &rfds);
      e->waiting_to_read.detach(we);
    }
    else {
      FD_CLR(fd, &wfds);
      e->waiting_to_write.detach(we);
    }

    FD_CLR(fd, &err_fds);

    assert(e && e->fd == fd);

    // Are we removing the largest fd we have?
    if (e->mask == 0 && e->fd == max_fd) {
      // Update max_fd when we remove the largest fd defined
      max_fd = 0;
      for (auto& e : entries) {
        if (e.mask && e.fd > max_fd)
          max_fd = e.fd;
      }
    }

    SOCKET_ID new_max_fd = 0;
    for (auto ne : entries) {
      if (ne.mask != 0 && ne.fd > new_max_fd)
        new_max_fd = ne.fd;
    }
    assert(new_max_fd == max_fd);

  }

  int internal::TIOEvents::update() {

    // Amount of time to wait
    timeval tm;
    tm.tv_sec = 0;
    tm.tv_usec = 0;

    fd_set fds_to_read, fds_to_write;
    fd_set fds_with_err;

    memcpy(&fds_to_read, &rfds, sizeof(fd_set));
    memcpy(&fds_to_write, &wfds, sizeof(fd_set));
    memcpy(&fds_with_err, &err_fds, sizeof(fd_set));

    // Do a real wait
    int num_events = 0;
    auto retval = ::select((int)(max_fd + 1), &fds_to_read, &fds_to_write, &fds_with_err, &tm);
    if (retval > 0) {

      for (auto& e : entries) {

        if (!e.mask)
          continue;

        if (FD_ISSET(e.fd, &fds_with_err))
          printf("Socket %d has errors\n", (int)e.fd);

        // we were waiting a read op, and we can read now...
        if (((e.mask & TO_READ) && FD_ISSET(e.fd, &fds_to_read)) || FD_ISSET(e.fd, &fds_with_err)) {
          auto we = e.waiting_to_read.detachFirst< TWatchedEvent >();
          if (we) {
            assert(we->io.fd == e.fd);
            assert(we->event_type == EVT_SOCKET_IO_CAN_READ);
            wakeUp(we);
          }
        }

        if (((e.mask & TO_WRITE) && FD_ISSET(e.fd, &fds_to_write)) || FD_ISSET(e.fd, &fds_with_err)) {
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
		internal::num_loops++;
    internal::io_events.update();
    checkTimeoutEvents();
    return internal::runActives();
  }

  // --------------------------------------------
  int internal::runActives() {

    int nactives = 0;

    // coros can be enlarged inside the loop, watch the iterator
    // don't become invalidated
    int i = 0;
    while (i < coros.size()) {
      auto co = coros[i];
      ++i;
      assert(co);
      
      // Skip the free's
      if (co->state == TCoro::FREE)
        continue;

      if (co->state == TCoro::WAITING_FOR_EVENT) {
        ++nactives;
        continue;
      }

      // The 'waiting for condition' must be checked on each try/run
      if (co->state == TCoro::WAITING_FOR_CONDITION) {
        if (co->must_wait()) {
          ++nactives;
          continue;
        }
        co->state = TCoro::RUNNING;
      }
      else {
        assert(co->state == TCoro::RUNNING);
      }
      
      co->resume();

      if ( co->state == TCoro::RUNNING 
        || co->state == TCoro::WAITING_FOR_CONDITION 
        || co->state == TCoro::WAITING_FOR_EVENT       // If by resuming we become WaitingForEvent, we are in fact an active co
        )
        ++nactives;

    }

    return nactives;
  }

}
