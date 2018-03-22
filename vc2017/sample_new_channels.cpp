#include <cstdarg>
#include <cstdio>
#include <vector>
#include "sample.h"

using namespace Coroutines;

struct TBaseChan {
  bool   is_closed = false;
  TList  waiting_for_push;
  TList  waiting_for_pull;

public:
  
  void close() {
    is_closed = true;
    // Wake up all threads waiting for me...
    // Waiting for pushing...
    while (auto we = waiting_for_push.detachFirst< TWatchedEvent >())
      wakeUp(we);
    // or waiting for pulling...
    while (auto we = waiting_for_pull.detachFirst< TWatchedEvent >())
      wakeUp(we);
  }

  bool closed() const { return is_closed; }

  virtual bool pull(      void* obj, size_t nbytes) = 0;
  virtual bool push(const void* obj, size_t nbytes) = 0;
};

struct TMemChan : public TBaseChan {
  
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

public:
  
  TMemChan(size_t new_max_elems, size_t new_bytes_per_elem) {
    bytes_per_elem = new_bytes_per_elem;
    max_elems = new_max_elems;
    data.resize(bytes_per_elem * max_elems);
  }

  ~TMemChan() {
    close();
  }

};

struct TIOChan : public TBaseChan {

};

struct TTimeChan : public TBaseChan {

};

struct TChanHandle {
  uint32_t class_id : 4;
  uint32_t index    : 12;
  uint32_t age      : 16;
  uint32_t asU32() const { return *((uint32_t*)this); }
  void fromU32(uint32_t new_u32) { *((uint32_t*)this) = new_u32; }
  TChanHandle() {
    class_id = index = age = 0;
  }
  TChanHandle(int32_t new_id) {
    fromU32(new_id);
  }
};

std::vector<TMemChan*> all_mem_channels;

// -------------------------------------------------------------
template< typename T >
int32_t newChanMem(int max_capacity = 1) {
  size_t bytes_per_elem = sizeof( T );
  TMemChan* c = new TMemChan(max_capacity, bytes_per_elem);
  all_mem_channels.push_back(c);
  return (int32_t) all_mem_channels.size() - 1;
}

template< typename T>
bool push(int32_t cid, const T& obj) {
  
  TChanHandle h_chan(cid);

  // The channel id is valid?
  if (cid < 0 || cid >= all_mem_channels.size())
    return false;

  TBaseChan* c = all_channels[cid];
  assert(c);

  if (c->closed())
    return false;

  return c->push(&obj, sizeof(T));

  //while (c->full() && !closed()) {
  //  TWatchedEvent evt(this, obj, EVT_CHANNEL_CAN_PUSH);
  //  wait(&evt, 1);
  //}
  //if (closed())
  //  return false;
  //pushBytes(&obj, sizeof(obj));

  return c->push<T>(obj);
}


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
// Wait for any of the two coroutines to finish or timeout
void sample_new_channels() {

  auto co = start([]() {

    int32_t c1 = newChanMem<int>(4);
    push(c1, 2);


  });


}
