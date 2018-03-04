#ifndef INC_COROUTINES_IO_EVENTS_H_
#define INC_COROUTINES_IO_EVENTS_H_

#include <vector>
#include <cassert>

// -----------------------------------------------------------
#ifdef _WIN32

#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>             // fd_set Family
#include <ws2tcpip.h>

typedef SOCKET           SOCKET_ID;
typedef sockaddr_in      TSockAddress;

#else     // -----------------------------------------------------------

#include <sys/select.h>
#include <sys/types.h>
#include <netinet/in.h>
typedef int              SOCKET_ID;
typedef sockaddr_in      TSockAddress;

#endif    // -----------------------------------------------------------

#include "list.h"

namespace Coroutines {

  struct TWatchedEvent;

  namespace internal {

    struct TIOEvents {

      struct TEntry {
        SOCKET_ID       fd;
        int             mask = 0;
        TList           waiting_to_read;
        TList           waiting_to_write;
      };

      typedef std::vector< TEntry > VDescriptors;
      SOCKET_ID    max_fd = 0;
      VDescriptors entries;

      fd_set       rfds;
      fd_set       wfds;
      fd_set       err_fds;

      TEntry* find(SOCKET_ID fd) {
        for (auto& e : entries) {
          if (e.fd == fd)
            return &e;
        }
        TEntry new_e;
        new_e.fd = fd;
        entries.push_back(new_e);
        return &entries.back();
      }

    public:

      TIOEvents() {
        FD_ZERO(&rfds);
        FD_ZERO(&wfds);
        FD_ZERO(&err_fds);
      }

      enum eMode {
        TO_READ = 1,
        TO_WRITE = 2,
      };

      void add(TWatchedEvent* we);
      void del(TWatchedEvent* we);
      int update();
    };
  }

}

#endif
