#ifndef INC_COROUTINES_CHANNEL_H_
#define INC_COROUTINES_CHANNEL_H_

#include <cinttypes>
#include "coroutines.h"

namespace Coroutines {

  // -------------------------------------------------------
  // -------------------------------------------------------
  // -------------------------------------------------------
  enum eChannelType { CT_INVALID = 0, CT_TIMER = 1, CT_MEMORY, CT_IO };
  struct TChanHandle {
    eChannelType class_id : 4;
    uint32_t     index : 12;
    uint32_t     age : 16;

    uint32_t asU32() const { return *((uint32_t*)this); }
    void fromU32(uint32_t new_u32) { *((uint32_t*)this) = new_u32; }

    TChanHandle() {
      class_id = CT_INVALID;
      index = age = 0;
    }

    explicit TChanHandle(uint32_t new_u32) {
      fromU32(new_u32);
    }

    TChanHandle(eChannelType channel_type, int32_t new_index) {
      class_id = channel_type;
      index = new_index;
      age = 1;
    }
    bool operator==(const TChanHandle h) const {
      return h.asU32() == asU32();
    }

  };

  // -------------------------------------------------------------
  namespace internal {

    // --------------------------------------------------------------
    class TBaseChan {
    protected:
      bool         is_closed = false;

    public:

      TChanHandle  handle;
      TList        waiting_for_push;
      TList        waiting_for_pull;

      void close() {
        is_closed = true;
        // Wake up all coroutines waiting for me...
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
  
      static TBaseChan* findChannelByHandle(TChanHandle h);
    };

    // --------------------------------------------------------------
    TChanHandle createTypedChannel(size_t max_capacity, size_t bytes_per_elem);
  }

  // -------------------------------------------------------------
  // Create a new 'typed' handle to channel
  template< typename T >
  struct TTypedChannel : public TChanHandle {
    TTypedChannel<T>(TChanHandle h) : TChanHandle(h) {};
    static TTypedChannel<T> create(size_t max_capacity = 1) {
      return internal::createTypedChannel(max_capacity, sizeof(T));
    }
  };

  // -------------------------------------------------------------
  template< typename T>
  bool push(TTypedChannel<T> cid, const T& obj) {

    auto c = internal::TBaseChan::findChannelByHandle(cid);
    if (!c || c->closed())
      return false;

    return c->push(&obj, sizeof(T));
  }

  // -------------------------------------------------------------
  template< typename T>
  bool pull(TTypedChannel<T> cid, T& obj) {

    auto c = internal::TBaseChan::findChannelByHandle(cid);
    if (!c || (c->closed() && c->empty()))
      return false;

    return c->pull(&obj, sizeof(T));
  }

  // -------------------------------------------------------------
  // Read discarting the data. 
  bool pull(TChanHandle cid);
  
  // Closes channel
  bool close(TChanHandle cid);

  // -------------------------------------------------------------
  template< typename T >
  bool operator<<(T& p, TTypedChannel<T> c) {
    return pull(c, p);
  }

  template< typename T >
  bool operator<<(TTypedChannel<T> c, T p) {
    return push(c, p);
  }

  // -------------------------------------------------------------
  bool operator<<(TTimeStamp& value, TChanHandle cid);
  TChanHandle every(TTimeDelta interval_time);
  TChanHandle after(TTimeDelta interval_time);

}

#endif
