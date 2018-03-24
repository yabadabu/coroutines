#include <cstdarg>
#include <cstdio>
#include <vector>
#include "sample.h"
#include "coroutines/choose.h"

using namespace Coroutines;

// -------------------------------------------------------
class TMemChan : public TBaseChan {
  
  typedef uint8_t u8;

  size_t            bytes_per_elem = 0;
  size_t            max_elems = 0;
  size_t            nelems_stored = 0;
  size_t            first_idx = 0;
  std::vector< u8 > data;

  u8* addrOfItem(size_t idx) {
    assert(!data.empty());
    assert(data.data());
    assert(idx < max_elems);
    return data.data() + idx * bytes_per_elem;
  }

  bool full() const { return nelems_stored == max_elems; }
  bool empty() const { return nelems_stored == 0; }

  void pushBytes(const void* user_data, size_t user_data_size);
  void pullBytes(void* user_data, size_t user_data_size);

public:
  
  TMemChan(size_t new_max_elems, size_t new_bytes_per_elem) {
    bytes_per_elem = new_bytes_per_elem;
    max_elems = new_max_elems;
    data.resize(bytes_per_elem * max_elems);
  }

  ~TMemChan() {
    close();
  }

  bool pull(void* addr, size_t nbytes) override {

    assert(this);
    assert(addr);
    assert(nbytes > 0);
    assert(nbytes == bytes_per_elem);
    while (empty() && !closed()) {
      TWatchedEvent evt(handle.asU32(), addr, nbytes, EVT_NEW_CHANNEL_CAN_PULL);
      wait(&evt, 1);
    }

    if (closed() && empty())
      return false;
    
    pullBytes(addr, nbytes);
    return true;
  }

  bool push(const void* addr, size_t nbytes) override {
    
    assert(addr);
    assert(nbytes > 0);
    assert(nbytes == bytes_per_elem);

    while (full() && !closed()) {
      TWatchedEvent evt(handle.asU32(), addr, nbytes, EVT_NEW_CHANNEL_CAN_PUSH);
      wait(&evt, 1);
    }
    
    if (closed())
      return false;
    
    pushBytes(addr, nbytes);
    return true;
  }

};


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
  assert(user_data);
  assert(nelems_stored > 0);
  assert(user_data_size == bytes_per_elem);
  if (bytes_per_elem)
    memcpy(user_data, addrOfItem(first_idx), bytes_per_elem);
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

    TWatchedEvent we(time_for_event);
    int idx = wait(&we, 1);
    if (idx == -1)
      return false;
    prepareNext();
    return true;
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

template< typename T >
TChanHandle newChanMem(int max_capacity = 1) {
  size_t bytes_per_elem = sizeof( T );
  TMemChan* c = new TMemChan(max_capacity, bytes_per_elem);
  return registerChannel(c, eChannelType::CT_MEMORY);
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
template< typename T>
bool push(TChanHandle cid, const T& obj) {
  
  TBaseChan* c = TBaseChan::findChannelByHandle(cid);
  if (!c || c->closed())
    return false;

  return c->push(&obj, sizeof(T));
}

// -------------------------------------------------------------
template< typename T>
bool pull(TChanHandle cid, T& obj) {

  TBaseChan* c = TBaseChan::findChannelByHandle(cid);
  if (!c || ( c->closed() && c->empty() ))
    return false;

  return c->pull(&obj, sizeof(T));
}

// -------------------------------------------------------------
// For the time channels
bool pull(TChanHandle cid) {
  TBaseChan* c = TBaseChan::findChannelByHandle(cid);
  if (!c || c->closed())
    return false;
  return c->pull(nullptr, 0);
}

// -------------------------------------------------------------
// -------------------------------------------------------------
// -------------------------------------------------------------
template< typename T >
bool operator<<(T& p, TChanHandle c) {
  return pull(c, p);
}

template< typename T >
bool operator<<(TChanHandle c, T&& p) {
  return push(c, p);
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

// ---------------------------------------------------------
TChanHandle new_boring(const char* label, TTimeDelta min_time = 0) {
  auto sc = newChanMem<const char*>();
  start([sc, label, min_time]() {
    while (true) {
      dbg("Try to push %sto c:%08x\n", label, sc);
      if (!push(sc, label))
        break;
      dbg("Pushed %s, no waiting a bit\n", label);
      Time::sleep(min_time + Time::milliseconds(rand() % 1000));
    }
  });
  return sc;
}

// -------------------------------------------------
THandle new_readChannel(TChanHandle c, int max_reads) {
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

  auto t1 = every(Time::seconds(1));
  auto t2 = after(Time::seconds(3));

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

// -------------------------------------------
template< typename T >
struct ifNewCanPullDef {
  TChanHandle                  channel = 0;
  T                            obj;                 // Temporary storage to hold the recv data
  std::function<void(T& obj)>  cb;
  ifNewCanPullDef(TChanHandle new_channel, std::function< void(T) >&& new_cb)
    : channel(new_channel)
    , cb(new_cb)
  { }
  void declareEvent(TWatchedEvent* we) {
    *we = TWatchedEvent(channel.asU32(), &obj, sizeof( T ), eEventType::EVT_NEW_CHANNEL_CAN_PULL);
  }
  void run() {
    dbg("choose.can pull fired from channel c:%08x\n", channel);
    if (pull(channel, obj))
      cb(obj);
  }
};


// Helper function to deduce the arguments in a fn, not as the ctor args
template< typename T, typename TFn >
ifNewCanPullDef<T> ifNewCanPull(TChanHandle chan, TFn&& new_cb) {
  return ifNewCanPullDef<T>(chan, new_cb);
}

void test_new_choose() {
  TSimpleDemo demo("test_new_choose");

  auto c1 = new_boring("John", Time::seconds(1));
  auto c2 = new_boring("Peter", Time::seconds(1));
  auto o1 = newChanMem<const char*>();

  auto coA = start([c1,c2,o1]() {
    while (true) {
      int n = choose(
        ifNewCanPull<const char*>(c1, [c1, o1](const char* msg) {
          dbg("Hi, I'm A and pulled data %s\n", msg);
          push(o1, msg);
        }),
        ifNewCanPull<const char*>(c2, [c2, o1](auto msg) {
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

    bool b;
    b << done;
    dbg("Jobs work finished\n");
  });

}

void sample_new_channels() {
  test_go_closing_channels();
  //test_new_choose();
  //test_every_and_after();
}


// Can't push (will block) unless there is someone than is pulling
// unless the channel is buffered


