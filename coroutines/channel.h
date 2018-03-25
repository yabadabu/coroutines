#ifndef INC_COROUTINES_CHANNEL_H_
#define INC_COROUTINES_CHANNEL_H_

#include <cinttypes>
#include "coroutines.h"

namespace Coroutines {

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


    // -------------------------------------------------------
    template< typename T >
    class TMemChan : public TBaseChan {

      typedef uint8_t u8;

      size_t            max_elems = 0;
      size_t            nelems_stored = 0;
      size_t            first_idx = 0;
      std::vector< T >  data;

      T* addrOfItem(size_t idx) {
        assert(!data.empty());
        assert(data.data());
        assert(idx < max_elems);
        return data.data() + idx;
      }

      bool full() const { return nelems_stored == max_elems; }
      bool empty() const { return nelems_stored == 0; }

      // -------------------------------------------------------------
      void pushObjData(T& new_data) {
        assert(nelems_stored < max_elems);
        assert(!closed());
        data[ (first_idx + nelems_stored) % max_elems ] = std::move( new_data );
        ++nelems_stored;

        // For each elem pushes, wakeup one waiter to pull
        auto we = waiting_for_pull.detachFirst< TWatchedEvent >();
        if (we) {
          assert(we->channel.handle == handle);
          assert(we->event_type == EVT_CHANNEL_CAN_PULL);
          wakeUp(we);
        }
      }

      // -------------------------------------------------------------
      void pullObjData(T& new_data) {
        assert(data.data());
        assert(nelems_stored > 0);
          
        new_data = std::move(data[first_idx]);
        
        --nelems_stored;
        first_idx = (first_idx + 1) % max_elems;

        // For each elem pulled, wakeup one waiter to push
        auto we = waiting_for_push.detachFirst< TWatchedEvent >();
        if (we) {
          assert(we->channel.handle == handle);
          assert(we->event_type == EVT_CHANNEL_CAN_PUSH);
          wakeUp(we);
        }

      }


    public:

      TMemChan(size_t new_max_elems) {
        max_elems = new_max_elems;
        data.resize(max_elems);
      }

      ~TMemChan() {
        close();
      }

      bool pullObj(T& obj) {

        assert(this);

        while (empty() && !closed()) {
          TWatchedEvent evt(handle, EVT_CHANNEL_CAN_PULL);
          wait(&evt, 1);
        }

        if (closed() && empty())
          return false;

        pullObjData(obj);
        return true;
      }

      bool pushObj( T& obj) {

        while (full() && !closed()) {
          TWatchedEvent evt(handle, EVT_CHANNEL_CAN_PUSH);
          wait(&evt, 1);
        }

        if (closed())
          return false;

        pushObjData(obj);
        return true;
      }

    };

    // --------------------------------------------------------------
    TChanHandle registerChannel(TBaseChan* c, eChannelType channel_type);

    template< typename T >
    TChanHandle createTypedChannel(size_t max_capacity) {
      auto c = new TMemChan<T>(max_capacity);
      return registerChannel(c, eChannelType::CT_MEMORY);
    }

  }

  // -------------------------------------------------------------
  // Create a new 'typed' handle to channel
  template< typename T >
  struct TTypedChannel : public TChanHandle {
    TTypedChannel<T>(TChanHandle h) : TChanHandle(h) {};
    static TTypedChannel<T> create(size_t max_capacity = 1) {
      return internal::createTypedChannel<T>(max_capacity);
    }
  };

  // -------------------------------------------------------------
  template< typename T>
  bool push(TTypedChannel<T> cid, T& obj) {

    auto c = internal::TBaseChan::findChannelByHandle(cid);
    if (!c || c->closed())
      return false;

    auto tc = (internal::TMemChan<T> *)c;
    return tc->pushObj(obj);
  }

  // -------------------------------------------------------------
  template< typename T>
  bool pull(TTypedChannel<T> cid, T& obj) {

    auto c = internal::TBaseChan::findChannelByHandle(cid);
    if (!c || (c->closed() && c->empty()))
      return false;
    auto tc = (internal::TMemChan<T> *)c;
    return tc->pullObj(obj);
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
