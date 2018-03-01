#ifndef INC_COROUTINE_IO_CHANNEL_H_
#define INC_COROUTINE_IO_CHANNEL_H_

#include "coroutines/coroutines.h"
#include "coroutines/io_address.h"

namespace Coroutines {

  class CIOChannel {
    static const SOCKET_ID invalid_socket_id = ~(SOCKET_ID(0));
    SOCKET_ID  fd = invalid_socket_id;
    bool       setNonBlocking();
  public:
    bool       isValid() const { return fd != invalid_socket_id; }
    bool       listen(const TNetAddress& serving_addr);
    CIOChannel accept();
    bool       connect(const TNetAddress &remote_server, int timeout_sec);
    // Will block until all bytes have been recv/sent
    bool       recv(void* dest_buffer, size_t bytes_to_read);
    bool       send(const void* src_buffer, size_t bytes_to_send);
    // Will return -1 if no bytes can been read. Will block until something is read.
    int        recvUpTo(void* dest_buffer, size_t max_bytes_to_read);
    void       close();

    template< typename T >
    bool recv(T& obj) {
      return recv(&obj, sizeof(T));
    }
    template< typename T >
    bool send(const T& obj) {
      return send(&obj, sizeof(T));
    }

  };

}

#endif


