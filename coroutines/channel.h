#ifndef INC_COROUTINES_CHANNEL_H_
#define INC_COROUTINES_CHANNEL_H_

#include <cinttypes>
#include "coroutines.h"

namespace Coroutines {

  typedef uint8_t u8;

  // ----------------------------------------
  class TRawChannel {
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
  
  protected:
    void pushBytes(const void* user_data, size_t user_data_size);
    void pullBytes(void* user_data, size_t user_data_size);
    TRawChannel() = default;
    TRawChannel(size_t new_max_elems, size_t new_bytes_per_elem) {
      bytes_per_elem = new_bytes_per_elem;
      max_elems = new_max_elems;
      data = new u8[bytes_per_elem * max_elems];
    }

  public:

    ~TRawChannel() {
      close();
      bytes_per_elem = 0;
      delete[] data;
    }

    size_t bytesPerElem() const { return bytes_per_elem; }
    bool closed() const { return is_closed; }
    bool empty() const { return nelems_stored == 0; }
    bool full() const { return nelems_stored == max_elems; }
    void close();
    size_t size() const { return nelems_stored; }

    // Without blocking
    bool canPull() const { return !empty(); }
    bool canPush() const { return !full(); }
  };

  // -----------------------------------------------------
  template< typename T >
  class TChannel : public TRawChannel {
  public:
    TChannel(size_t new_max_elems = 1) :
      TRawChannel(new_max_elems, sizeof(T))
    {}
    // returns true if the object can be pushed
    bool push(const T& obj) {
      assert(this);
      assert(&obj);
      while (full() && !closed()) {
        TWatchedEvent evt(this, obj, EVT_CHANNEL_CAN_PUSH);
        wait(&evt, 1);
      }
      if (closed())
        return false;
      pushBytes(&obj, sizeof(obj));
      return true;
    }

    // returns true if the object can be pulled
    bool pull(T& obj) {
      assert(this);
      assert(&obj);
      while (empty() && !closed()) {
        TWatchedEvent evt(this, obj, EVT_CHANNEL_CAN_PULL);
        wait(&evt, 1);
      }

      if (closed() && empty())
        return false;
      pullBytes(&obj, sizeof(T));
      return true;
    }
  };




  // --------------------------------------------------------------
  class TBaseChan {
  protected:
    bool     is_closed = false;

  public:

    int32_t  h_channel = 0;
    TList    waiting_for_push;
    TList    waiting_for_pull;

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
    virtual bool empty() const { return true; }
    virtual bool full() const { return false; }

    virtual bool pull(void* obj, size_t nbytes) { return false; }
    virtual bool push(const void* obj, size_t nbytes) { return false; }
  
    static TBaseChan* findChannelByHandle(int cid);
  };
  


}

#endif
