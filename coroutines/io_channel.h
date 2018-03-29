#ifndef INC_COROUTINE_IO_CHANNEL_H_
#define INC_COROUTINE_IO_CHANNEL_H_

#include "coroutines.h"
#include "channel.h"

namespace Coroutines {

  namespace Net {

    struct TSocket {
#ifdef WIN32
      typedef SOCKET TOSSocket;
#else
      typedef int    TOSSocket;
#endif
      static const TOSSocket invalid = ~(TOSSocket(0));
      TOSSocket s = invalid;
      TSocket() = default;
      TSocket(TOSSocket ns) : s(ns) {};
      operator bool() const {
        return s != invalid;
      }
    };

    // Will yield until the connection can be stablished
    TSocket connect(const char* addr, int port);

    // Will yield until the an incomming connection is recv
    TSocket accept(TSocket server);

    // use ("127.0.0.1", port, AF_INET ) or ("::", port, AF_INET6)
    TSocket listen(const char* bind_addr, int port, int af);

    bool close(TSocket s);

    // Will yield until all bytes have been sent
    // Returns true if all bytes could be send, or false if there was an error
    bool send(TSocket s, const void* src_buffer, size_t bytes_to_send);
    
    // Will yield until all bytes have been recv
    // Returns true if all bytes could be read, or false if there was an error
    bool recv(TSocket s, void* dst_buffer, size_t bytes_to_recv);
    
    // Will return -1 if no bytes can been read. Will block until something can be read.
    // Returns number of bytes recv;
    int  recvUpTo(TSocket s, void* dest_buffer, size_t max_bytes_to_read);

    template< typename T >
    bool recv(TSocket s, T& obj) {
      return recv(s, &obj, sizeof(T));
    }

    template< typename T >
    bool send(TSocket s, const T& obj) {
      return send(s, &obj, sizeof(T));
    }


  }



}

#endif


