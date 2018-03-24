#ifndef INC_COROUTINES_CHANNEL_H_
#define INC_COROUTINES_CHANNEL_H_

#include <cinttypes>
#include "coroutines.h"

namespace Coroutines {

  typedef uint8_t u8;

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

      while (empty() && !closed()) {
        TWatchedEvent evt(handle.asU32(), EVT_NEW_CHANNEL_CAN_PULL);
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
        TWatchedEvent evt(handle.asU32(), EVT_NEW_CHANNEL_CAN_PUSH);
        wait(&evt, 1);
      }

      if (closed())
        return false;

      pushBytes(addr, nbytes);
      return true;
    }

  };

  // -------------------------------------------------------------
  // Create a new 'typed' handle to channel
  template< typename T >
  struct TTypedChannel : public TChanHandle {
    TTypedChannel<T>(TChanHandle h) : TChanHandle(h) {};
    static TTypedChannel<T> create(size_t max_capacity = 1);
  };

  template< typename T >
  TTypedChannel<T> newChanMem(size_t max_capacity = 1) {
    size_t bytes_per_elem = sizeof(T);
    TMemChan* c = new TMemChan(max_capacity, bytes_per_elem);
    return registerChannel(c, eChannelType::CT_MEMORY);
  }

  template< typename T >
  TTypedChannel<T> TTypedChannel<T>::create(size_t max_capacity) {
    return newChanMem<T>(max_capacity);
  }


  // -------------------------------------------------------------
  template< typename T>
  bool push(TTypedChannel<T> cid, const T& obj) {

    TBaseChan* c = TBaseChan::findChannelByHandle(cid);
    if (!c || c->closed())
      return false;

    return c->push(&obj, sizeof(T));
  }

  // -------------------------------------------------------------
  template< typename T>
  bool pull(TTypedChannel<T> cid, T& obj) {

    TBaseChan* c = TBaseChan::findChannelByHandle(cid);
    if (!c || (c->closed() && c->empty()))
      return false;

    return c->pull(&obj, sizeof(T));
  }

  // -------------------------------------------------------------
  // Read discarting the data. 
  bool pull(TChanHandle cid);
  bool closeChan(TChanHandle cid);
  TChanHandle registerChannel(TBaseChan* c, eChannelType channel_type);

  // -------------------------------------------------------------
  template< typename T >
  bool operator<<(T& p, TTypedChannel<T> c) {
    return pull(c, p);
  }

  template< typename T >
  bool operator<<(TTypedChannel<T> c, T p) {
    return push(c, p);
  }


}

#endif
