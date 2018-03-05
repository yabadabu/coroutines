#include "channel.h"
#include "coroutines.h"

namespace Coroutines {

  void TChannel::push(const void* user_data, size_t user_data_size) {
    assert(user_data);
    assert(data);
    assert(nelems_stored < max_elems);
    assert(user_data_size == bytes_per_elem);
    assert(!closed());
    if (bytes_per_elem)
      memcpy(addrOfItem((first_idx + nelems_stored) % max_elems), user_data, bytes_per_elem);
    ++nelems_stored;

    // For each elem pushes, wakeup one waiter to pull
    auto we = waiting_for_pull.detachFirst< TWatchedEvent >();
    if (we) {
      assert(we->channel.channel == this);
      assert(we->event_type == EVT_CHANNEL_CAN_PULL);
      wakeUp(we);
    }
  }

  void TChannel::pull(void* user_data, size_t user_data_size) {
    assert(data);
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
      assert(we->channel.channel == this);
      assert(we->event_type == EVT_CHANNEL_CAN_PUSH);
      wakeUp(we);
    }

  }

  void TChannel::close() { 
    is_closed = true; 
    // Wake up all threads waiting for me...
    // Waiting for pushing...
    while (auto we = waiting_for_push.detachFirst< TWatchedEvent >())
      wakeUp(we);
    // or waiting for pulling...
    while (auto we = waiting_for_pull.detachFirst< TWatchedEvent >())
      wakeUp(we);
  }

}
