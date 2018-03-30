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

      bool         detachOneWaitingForPull();
      bool         detachOneWaitingForPush();

    public:

      TChanHandle  handle;
      TList        waiting_for_push;
      TList        waiting_for_pull;

      virtual ~TBaseChan() {}

      void close() {

        is_closed = true;

        // Wake up all coroutines waiting for me...
        // Waiting for pushing...
        while (detachOneWaitingForPush());
        // or waiting for pulling...
        while (detachOneWaitingForPull());

      }

      bool closed() const { return is_closed; }
      virtual bool empty() const { return true; }
      virtual bool full() const { return false; }

      static TBaseChan* findChannelByHandle(TChanHandle h);
    };


    // -------------------------------------------------------
    template< typename T >
    class TMemChan : public TBaseChan {

      size_t            max_elems = 0;
      size_t            nelems_stored = 0;
      size_t            first_idx = 0;
      std::vector< T >  data;

      bool full() const { return nelems_stored == max_elems; }
      bool empty() const { return nelems_stored == 0; }

      // -------------------------------------------------------------
      void pushObjData(T& new_data) {
        assert(nelems_stored < max_elems);
        assert(!closed());

        data[ (first_idx + nelems_stored) % max_elems ] = std::move( new_data );
        ++nelems_stored;
        
        detachOneWaitingForPull();
      }

      // -------------------------------------------------------------
      void pullObjData(T& new_data) {
        assert(data.data());
        assert(nelems_stored > 0);
          
        new_data = std::move(data[first_idx]);
        
        --nelems_stored;
        first_idx = (first_idx + 1) % max_elems;

        detachOneWaitingForPush();
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

        while (empty() && !closed()) 
          wait(canRead(handle));

        if (empty() && closed())
          return false;

        pullObjData(obj);

        return true;
      }

      bool pushObj( T& obj) {

        while (full() && !closed())
          wait(canWrite(handle));

        if (closed())
          return false;

        pushObjData(obj);

        return true;
      }

    };

    // --------------------------------------------------------------
    TChanHandle registerChannel(TBaseChan* c, TChanHandle::eClassID channel_type);

    template< typename T >
    TChanHandle createTypedChannel(size_t max_capacity) {
      auto c = new TMemChan<T>(max_capacity);
      return registerChannel(c, TChanHandle::eClassID::CT_MEMORY);
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
  
  // Closes channel
  bool close(TChanHandle cid);

  // Check if the given channel handle is valid.
  bool isChannel(TChanHandle cid);

  // -------------------------------------------------------------
  template< typename T >
  bool operator<<(T& obj, TTypedChannel<T> cid) {
    auto c = internal::TBaseChan::findChannelByHandle(cid);
    if (!c || (c->closed() && c->empty()))
      return false;
    auto tc = (internal::TMemChan<T> *)c;
    return tc->pullObj(obj);
  }

  template< typename T >
  bool operator<<(TTypedChannel<T> cid, T obj) {
    auto c = internal::TBaseChan::findChannelByHandle(cid);
    if (!c || c->closed())
      return false;

    auto tc = (internal::TMemChan<T> *)c;
    return tc->pushObj(obj);
  }

  // -------------------------------------------------------------
  struct TTimeHandle : public TChanHandle {
    TTimeHandle(const TChanHandle& h) : TChanHandle(h) {}
    TTimeDelta timeForNextEvent() const;
  };
  bool operator<<(TTimeStamp& value, TTimeHandle cid);
  TTimeHandle every(TTimeDelta interval_time);
  TTimeHandle after(TTimeDelta interval_time);

  // Read discarting the data. 
  bool pull(TTimeHandle cid);

}

#endif
