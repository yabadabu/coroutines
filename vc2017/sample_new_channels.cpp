#include <cstdarg>
#include <cstdio>
#include <vector>
#include "sample.h"
#include "coroutines/choose.h"

using namespace Coroutines;

namespace Coroutines {

  void TMemChan::pushBytes(const void* user_data, size_t user_data_size) {
    assert(user_data);
    assert(data.data());
    assert(nelems_stored < max_elems);
    assert(user_data_size == bytes_per_elem);
    assert(!closed());
    if (bytes_per_elem)
      memcpy(addrOfItem((first_idx + nelems_stored) % max_elems), user_data, bytes_per_elem);
    ++nelems_stored;

    // For each elem pushes, wakeup one waiter to pull
    auto we = waiting_for_pull.detachFirst< TWatchedEvent >();
    if (we) {
      assert(we->nchannel.channel == handle.asU32());
      assert(we->event_type == EVT_NEW_CHANNEL_CAN_PULL);
      wakeUp(we);
    }
  }

  void TMemChan::pullBytes(void* user_data, size_t user_data_size) {
    assert(data.data());
    assert(nelems_stored > 0);
    if (user_data) {
      assert(user_data_size == bytes_per_elem);
      memcpy(user_data, addrOfItem(first_idx), bytes_per_elem);
    }
    --nelems_stored;
    first_idx = (first_idx + 1) % max_elems;

    // For each elem pulled, wakeup one waiter to push
    auto we = waiting_for_push.detachFirst< TWatchedEvent >();
    if (we) {
      assert(we->nchannel.channel == handle.asU32());
      assert(we->event_type == EVT_NEW_CHANNEL_CAN_PUSH);
      wakeUp(we);
    }

  }

}

// -------------------------------------------------------
class TIOChan : public TBaseChan {

};

// -------------------------------------------------------
class TTimeChan : public TBaseChan {
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
  : next( now() + amount_of_time_between_events )
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
    wes[1] = TWatchedEvent(handle.asU32(), eEventType::EVT_NEW_CHANNEL_CAN_PULL);
    int idx = wait(wes, 2);
    if (idx == -1)
      return false;
    
    if (obj) {
      assert(nbytes == sizeof(TTimeStamp));
      *(TTimeStamp*)obj = now();
    }

    prepareNext();

    // Return true only if the timer was really triggered
    return ( idx == 0 );
  }
};

std::vector<TBaseChan*> all_channels;

TBaseChan* TBaseChan::findChannelByHandle(TChanHandle h) {

  // The channel id is valid?
  if (h.index < 0 || h.index >= all_channels.size())
    return nullptr;

  TBaseChan* c = all_channels[h.index];
  assert(c);
  
  return (c->handle == h) ? c : nullptr;
}

// -------------------------------------------------------------
namespace Coroutines {

  bool closeChan(TChanHandle cid) {
    TBaseChan* c = TBaseChan::findChannelByHandle(cid);
    if (!c || c->closed())
      return false;
    c->close();
    return true;
  }

  TChanHandle registerChannel(TBaseChan* c, eChannelType channel_type) {
    all_channels.push_back(c);
    c->handle = TChanHandle(channel_type, (int32_t)all_channels.size() - 1);
    return c->handle;
  }

  TChanHandle every(TTimeDelta interval_time) {
    TTimeChan* c = new TTimeChan(interval_time, true);
    all_channels.push_back(c);
    return registerChannel(c, eChannelType::CT_TIMER);
  }

  TChanHandle after(TTimeDelta interval_time) {
    TTimeChan* c = new TTimeChan(interval_time, false);
    all_channels.push_back(c);
    return registerChannel(c, eChannelType::CT_TIMER);
  }

  // -------------------------------------------------------------
  // For the time channels
  bool pull(TChanHandle cid) {
    TBaseChan* c = TBaseChan::findChannelByHandle(cid);
    if (!c || c->closed())
      return false;
    return c->pull(nullptr, 0);
  }

}

// -------------------------------------------------------------
// -------------------------------------------------------------
// -------------------------------------------------------------

bool operator<<(TTimeStamp& value, TChanHandle cid) {
  TBaseChan* c = TBaseChan::findChannelByHandle(cid);
  if (!c || c->closed())
    return false;
  return c->pull(&value, sizeof(value));
}

//template< typename T >
//bool operator>>(T& p, TChannel<T>& c) {
//  return c.push(p);
//}
//
//template< typename T >
//bool operator>>(TChannel<T>& c, T& p) {
//  return c.pull(p);
//}


// -------------------------------------------------------------
// 
// 
// -------------------------------------------------------------

// ---------------------------------------------------------
TTypedChannel<const char*> new_boring(const char* label, TTimeDelta min_time = 0) {
  auto sc = newChanMem<const char*>();
  start([sc, label, min_time]() {
    while (true) {
      dbg("Try to push %sto c:%08x\n", label, sc);
      if (!( sc << label))
        break;
      dbg("Pushed %s, no waiting a bit\n", label);
      Time::sleep(min_time + Time::milliseconds(rand() % 1000));
    }
  });
  return sc;
}

// -------------------------------------------------
THandle new_readChannel(TTypedChannel<const char*> c, int max_reads) {
  return start([c, max_reads]() {
    for (int i = 0; i < max_reads; ++i) {
      const char* msg = nullptr;
      if (!pull(c, msg))
        break;
      dbg("Read [%d] %s\n", i, msg);
    }
    dbg("Bye\n");
  });
}


// ---------------------------------------------------------
void test_every_and_after() {
  TSimpleDemo demo("test_every_and_after");

  auto t1 = every(Time::seconds(3));
  auto t2 = after(Time::seconds(4));

  auto coT2 = start([t1, t2]() {
    pull(t2);
    dbg("After t2 timeouts..., closing the 'every' channel t1\n");
    closeChan(t1);
  });

  auto coT = start([t1]() {
    while (pull(t1))
      dbg(".\n");
    dbg("End of events\n");
  });

}


void test_new_choose() {
  TSimpleDemo demo("test_new_choose");

  auto c1 = new_boring("John", Time::seconds(1));
  auto c2 = new_boring("Peter", Time::seconds(1));
  auto o1 = newChanMem<const char*>();

  auto coA = start([c1,c2,o1]() {
    while (true) {
      int n = choose(
        ifNewCanPull(c1, [c1, o1](const char* msg) {
          dbg("Hi, I'm A and pulled data %s\n", msg);
          push(o1, msg);
        }),
        ifNewCanPull(c2, [c2, o1](auto msg) {
          dbg("Hi, I'm B and pulled data %s\n", msg);
          push(o1, msg);
        })
        //  ,
        //ifTimeout(400, []() {
        //  dbg("Timeout waiting for a or b\n");
        //})
        );
      dbg("Choose returned %d\n", n);
    }
  });

  auto co2 = new_readChannel(o1, 5);
  start([&]() {
    wait(co2);
    closeChan(c1);
    closeChan(c2);
    closeChan(o1);
  });
}

// --------------------------------------------------
// go will push, the recv, push, recv, push recv, close, done
// here will: push push push, close, recv, recv, recv, done
void test_go_closing_channels() {
  TSimpleDemo demo("test_go_closing_channels");
  auto jobs = newChanMem<int>(5);
  auto done = newChanMem<bool>();
  start([&]() {
    while (true) {
      int j;
      if (j << jobs) {
        dbg("Received job %d\n", j);
      }
      else {
        dbg("Received all jobs!\n");
        done << true;
        break;
      }
    }
  });

  start([&]() {
    for (int i = 1; i <= 3; ++i) {
      jobs << i;
      dbg("Sent job %d\n", i);
    }
    closeChan(jobs);
    dbg("sent all jobs, channel is now closed\n");

    pull(done);
    dbg("Jobs work finished\n");
  });

}


// --------------------------------------------------
void test_read_closed_channels() {
  TSimpleDemo demo("test_read_closed_channels");
  start([]() {
    auto queue = newChanMem<const char*>(2);
    queue << "one";
    queue << "two";
    closeChan(queue);
    const char* msg;
    while (msg << queue) {
      dbg("Recv %s\n", msg);
    }
  });
}

// --------------------------------------------------
void test_tickers() {
  TSimpleDemo demo("test_tickers");
  start([]() {
    auto ticker = every(500 * Time::MilliSecond);
    start([ticker]() {
      TTimeStamp ts;
      while (ts << ticker) {
        long num_secs, num_millisecs;
        getSecondsAndMilliseconds(ts, &num_secs, &num_millisecs);
        num_secs = num_secs % 60;
        long num_mins = num_secs / 60;
        dbg("Tick at %ld:%ld:%03ld\n", num_mins, num_secs, num_millisecs);
      }
    });
    Time::sleep(1600 * Time::MilliSecond);
    closeChan(ticker);
    dbg("Ticker stopped\n");
  });
}

// --------------------------------------------------
void go_worker(int id, TTypedChannel<int> jobs, TTypedChannel<int> results) {
  int j;
  while (j << jobs) {
    dbg("Worker %d started job %d\n", id, j);
    Time::sleep(Time::Second);
    dbg("Worker %d finished job %d\n", id, j);
    results << (j * 2);
  }
}

void test_go_worker_pool() {
  TSimpleDemo demo("test_go_worker_pool");
  start([]() {
    auto jobs = newChanMem<int>(100);
    auto results = newChanMem<int>(100);
    for (int i = 1; i <= 3; ++i) {
      start([&,i]() {   // We need by value
        go_worker(i, jobs, results);
      });
    }
    dbg("Sending work...\n");
    for (int i = 1; i <= 5; ++i)
      jobs << i;
    closeChan(jobs);
    dbg("Receiving results...\n");
    for (int i = 0; i < 5; ++i) {
      int r;
      r << results;
      dbg("Result %d is %d\n", i, r);
    }
    dbg("Done\n");
  });
}

void sample_new_channels() {
  //test_go_closing_channels();
  //test_every_and_after();
  //test_read_closed_channels();
  //test_tickers();
  test_go_worker_pool();
  //test_new_choose();
}


// Can't push (will block) unless there is someone than is pulling
// unless the channel is buffered



/*
int ch = newChannel<int>();
int ch = newChannel<int>(10);
push( ch, 2 );
push<float>( ch, 2.f );
int id;
pull( ch, id );
pull( ch, &id, sizeof(int));
pull<float>( ch, fdata );
closeChannel(ch);
deleteChannel( ch );

int ch = newTcpConnection(ip_addr)
push( ch, 2 );
pull( ch, id );
int nbytes = pullUpTo( ch, addr, max_bytes_to_pull );
deleteChannel( ch );

int ch_s = newTcpServer(ip_addr);
int ch_c = newTcpAccept( ch_s );
...
deleteChannel( ch_s );

// Time functions

*/
