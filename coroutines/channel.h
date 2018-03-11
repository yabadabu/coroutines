#ifndef INC_COROUTINES_CHANNEL_H_
#define INC_COROUTINES_CHANNEL_H_

#include <cinttypes>
#include "coroutines.h"

namespace Coroutines {

  typedef uint8_t u8;

  // ----------------------------------------
  class TChannel {
    size_t bytes_per_elem = 0;
    size_t max_elems      = 0;
    size_t nelems_stored  = 0;
    size_t first_idx      = 0;
    u8*    data           = nullptr;
    bool   is_closed      = false;

    u8* addrOfItem(size_t idx) {
      assert(data);
      assert(idx < max_elems);
      return data + idx * bytes_per_elem;
    }

  public:
    TList  waiting_for_push;
    TList  waiting_for_pull;

  public:
    TChannel() = default;
    TChannel(size_t new_max_elems, size_t new_bytes_per_elem) {
      bytes_per_elem = new_bytes_per_elem;
      max_elems = new_max_elems;
      data = new u8[bytes_per_elem * max_elems];
    }
    void push(const void* user_data, size_t user_data_size);
    void pull(void* user_data, size_t user_data_size);
    bool closed() const { return is_closed; }
    bool empty() const { return nelems_stored == 0; }
    bool full() const { return nelems_stored == max_elems; }
    void close();
    size_t bytesPerElem() const { return bytes_per_elem; }
    size_t size() const { return nelems_stored; }

    // Without blocking
    bool canPull() const { return !empty(); }
    bool canPush() const { return !full(); }
  };

  // -----------------------------------------------------
  // returns true if the object can be pulled
  template< typename TObj >
  bool pull(TChannel* ch, TObj& obj) {
    assert(ch);
    assert(&obj);
    while (ch->empty() && !ch->closed()) {
      TWatchedEvent evt(ch, obj, EVT_CHANNEL_CAN_PULL);
      wait(&evt, 1);
    }

    if (ch->closed() && ch->empty())
      return false;
    ch->pull(&obj, sizeof(obj));
    return true;
  }

  // returns true if the object can be pushed
  template< typename TObj >
  bool push(TChannel* ch, const TObj& obj) {
    assert(ch);
    assert(&obj);
    while (ch->full() && !ch->closed()) {
      TWatchedEvent evt(ch, obj, EVT_CHANNEL_CAN_PUSH);
      wait(&evt, 1);
    }
    if (ch->closed())
      return false;
    ch->push(&obj, sizeof(obj));
    return true;
  }

}

#endif
