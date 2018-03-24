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
      TWatchedEvent evt(h_channel, addr, nbytes, EVT_NEW_CHANNEL_CAN_PULL);
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
      TWatchedEvent evt(h_channel, addr, nbytes, EVT_NEW_CHANNEL_CAN_PUSH);
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
    assert(we->nchannel.channel == h_channel);
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
    assert(we->nchannel.channel == h_channel);
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

// -------------------------------------------------------
enum eChannelType { CT_INVALID = 0, CT_TIMER = 1, CT_MEMOY, CT_IO };
struct TChanHandle {
  eChannelType class_id : 4;
  uint32_t index        : 12;
  uint32_t age          : 16;
  
  uint32_t asU32() const { return *((uint32_t*)this); }
  void fromU32(uint32_t new_u32) { *((uint32_t*)this) = new_u32; }
  
  TChanHandle() {
    class_id = CT_INVALID;
    index = age = 0;
  }
  TChanHandle(uint32_t new_id) {
    fromU32(new_id);
  }
  TChanHandle(eChannelType channel_type, int32_t new_index) {
    class_id = channel_type;
    index = new_index;
    age = 1;
  }
};

std::vector<TMemChan*>  all_mem_channels;
std::vector<TTimeChan*> all_time_channels;

TBaseChan* TBaseChan::findChannelByHandle(int cid) {
  TChanHandle h(cid);

  if (h.class_id == eChannelType::CT_MEMOY) {
    // The channel id is valid?
    if (h.index < 0 || h.index >= all_mem_channels.size())
      return nullptr;

    TBaseChan* c = all_mem_channels[h.index];
    assert(c);
    return c;

  } else if(h.class_id == eChannelType::CT_TIMER) {
    // The channel id is valid?
    if (h.index < 0 || h.index >= all_time_channels.size())
      return nullptr;

    TBaseChan* c = all_time_channels[h.index];
    assert(c);
    return c;
  }

  return nullptr;
}

// -------------------------------------------------------------
bool closeChan(int32_t cid) {
  TBaseChan* c = TBaseChan::findChannelByHandle(cid);
  if (!c || c->closed())
    return false;
  c->close();
  return true;
}

template< typename T >
int32_t newChanMem(int max_capacity = 1) {
  size_t bytes_per_elem = sizeof( T );
  TMemChan* c = new TMemChan(max_capacity, bytes_per_elem);
  all_mem_channels.push_back(c);
  c->h_channel = TChanHandle(eChannelType::CT_MEMOY, (int32_t)all_mem_channels.size() - 1).asU32();
  return c->h_channel;
}

int32_t every(TTimeDelta interval_time) {
  TTimeChan* c = new TTimeChan(interval_time, true);
  all_time_channels.push_back(c);
  c->h_channel = TChanHandle(eChannelType::CT_TIMER, (int32_t)all_time_channels.size() - 1).asU32(); ;
  return c->h_channel;
}

int32_t after(TTimeDelta interval_time) {
  TTimeChan* c = new TTimeChan(interval_time, false);
  all_time_channels.push_back(c);
  c->h_channel = TChanHandle(eChannelType::CT_TIMER, (int32_t)all_time_channels.size() - 1).asU32(); ;
  return c->h_channel;
}

// -------------------------------------------------------------
template< typename T>
bool push(int32_t cid, const T& obj) {
  
  TBaseChan* c = TBaseChan::findChannelByHandle(cid);
  if (!c || c->closed())
    return false;

  return c->push(&obj, sizeof(T));
}

// -------------------------------------------------------------
template< typename T>
bool pull(int32_t cid, T& obj) {

  TBaseChan* c = TBaseChan::findChannelByHandle(cid);
  if (!c || c->closed())
    return false;

  return c->pull(&obj, sizeof(T));
}

// -------------------------------------------------------------
// For the time channels
bool pull(int32_t cid) {
  TBaseChan* c = TBaseChan::findChannelByHandle(cid);
  if (!c || c->closed())
    return false;
  return c->pull(nullptr, 0);
}



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
int new_boring(const char* label, TTimeDelta min_time = 0) {
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
THandle new_readChannel(int c, int max_reads) {
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

  int32_t t1 = every(Time::seconds(1));
  int32_t t2 = after(Time::seconds(3));

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
  int                          channel = 0;
  T                            obj;                 // Temporary storage to hold the recv data
  std::function<void(T& obj)>  cb;
  ifNewCanPullDef(int new_channel, std::function< void(T) >&& new_cb)
    : channel(new_channel)
    , cb(new_cb)
  { }
  void declareEvent(TWatchedEvent* we) {
    *we = TWatchedEvent(channel, &obj, sizeof( T ), eEventType::EVT_NEW_CHANNEL_CAN_PULL);
  }
  void run() {
    dbg("choose.can pull fired from channel c:%08x\n", channel);
    if (pull(channel, obj))
      cb(obj);
  }
};


// Helper function to deduce the arguments in a fn, not as the ctor args
template< typename T, typename TFn >
ifNewCanPullDef<T> ifNewCanPull(int chan, TFn&& new_cb) {
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

/*
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
    dbg("sent all jobs, channel is closed\n");

    bool b;
    b << done;
    dbg("Jobs work finished\n");
  });

}
*/

void sample_new_channels() {
  //test_go_closing_channels();
  //test_new_choose();
  //test_every_and_after();
}


// Can't push (will block) unless there is someone than is pulling
// unless the channel is buffered