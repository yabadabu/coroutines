#define _CRT_SECURE_NO_WARNINGS
#include "coroutines.h"

extern void dbg(const char *fmt, ...);
//#define dbg(...)

#ifdef _WIN32

#pragma comment(lib, "Ws2_32.lib")
#define sys_socket(x,y,z,port)	::socket( x, y, z )
#define sys_connect(id,addr,sz) ::connect( id, (const sockaddr*) addr, (int)sz )
#define sys_send                ::send
#define sys_recv                ::recv
#define sys_errno               ::WSAGetLastError()
#define sys_close               ::closesocket
#define sys_bind(id,addr,sz)    ::bind(id, (const sockaddr *) addr, (int)sz )
#define sys_accept(id,addr,sz)  ::accept( id, (sockaddr*) addr, sz )
#define sys_listen              ::listen

#define SYS_ERR_WOULD_BLOCK      WSAEWOULDBLOCK
#define SYS_ERR_CONN_IN_PROGRESS WSAEWOULDBLOCK

#else

#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <arpa/inet.h>

#define sys_socket(x,y,z,port)  ::socket( x, y, z )
#define sys_connect(id,addr,sz) ::connect( id, (const sockaddr*) addr, sz )
#define sys_send                ::send
#define sys_recv                ::recv
#define sys_errno               errno
#define sys_close               ::close
#define sys_bind(id,addr,sz)    ::bind(id, (const sockaddr *) addr, sz )
#define sys_accept(id,addr,sz)  ::accept( id, (sockaddr*) addr, (socklen_t*)sz )
#define sys_listen              ::listen

#define SYS_ERR_WOULD_BLOCK      EWOULDBLOCK
#define SYS_ERR_CONN_IN_PROGRESS EINPROGRESS

#endif

namespace Coroutines {

  namespace Net {

    namespace internal {

      bool setNonBlocking( TSocket sock ) {
        // set non-blocking
#if defined(O_NONBLOCK)
        int flags = fcntl(sock.s, F_GETFL, 0);
        if (flags == -1)
          flags = 0;
        auto rc = fcntl(sock.s, F_SETFL, flags | O_NONBLOCK);
#else
        u_long iMode = 1;
        auto rc = ioctlsocket(sock.s, FIONBIO, &iMode);
#endif
        if (rc != 0)
          dbg("Failed to set socket %d as non-blocking\n", sock.s);
        return rc == 0;
      }

      // ---------------------------------------------------------------------------
      int getSocketError(TSocket sock) {
        // Confirm we are really connected by checking the socket error
        int sock_err = 0;
        socklen_t sock_err_sz = sizeof(sock_err);
        int rc = getsockopt(sock.s, SOL_SOCKET, SO_ERROR, (char*)&sock_err, &sock_err_sz);
        return sock_err;
      }

    }

    using namespace internal;

    // ----------------------------------------------------------
    TSocket connect(const char* addr, int port) {

      assert(addr);
      assert(port > 0 && port < 65536);

      // Convert port to string "8081"
      char port_str[16];
      snprintf(port_str, sizeof(port_str) - 1, "%d", port);

      // Convert addr string to a list of address
      // Prepare to hold 
      struct addrinfo hints;
      memset(&hints, 0, sizeof(hints));
      hints.ai_family = AF_UNSPEC;
      hints.ai_socktype = SOCK_STREAM;
      struct addrinfo* host_info = nullptr;
      int rc = getaddrinfo(addr, port_str, &hints, &host_info);
      if (rc)
        return TSocket();

      // For each alternative proposed by getaddrinfo....
      struct addrinfo* target_addr = host_info;

      TSocket answer;

      while (target_addr) {

        // Create a socket of the family suggested
        auto new_fd = sys_socket(target_addr->ai_family, target_addr->ai_socktype, target_addr->ai_protocol, 0);
        if (new_fd < 0)
          break;

        auto sock = TSocket(new_fd);
        if (setNonBlocking(sock)) {

          // Now connect to that address
          int rc = sys_connect(sock.s, target_addr->ai_addr, target_addr->ai_addrlen);

          if (rc < 0) {
            int sys_err = sys_errno;
            if (sys_err == SYS_ERR_CONN_IN_PROGRESS) {
              dbg("FD %d waiting to connect\n", sock.s);
              TWatchedEvent we(sock.s, EVT_SOCKET_IO_CAN_WRITE);
              int n = wait(&we, 1);
              if (n == 0) {

                // Confirm we are really connected by checking the socket error
                int sock_err = getSocketError(sock.s);

                // All ok, no errors
                if (sock_err == 0) {
                  answer = sock.s;
                  break;
                }

                // The expected error in this case is Conn Refused when there is no server
                // in the remote address. Other erros, I prefer to report them
                if (sock_err != ECONNREFUSED)
                  dbg("connect.failed getsockopt( %d ) (err=%08x %s)\n", sock.s, sock_err, strerror(sock_err));
              }
            }
          }
        }

        sys_close(sock.s);

        // Try next candidate
        target_addr = target_addr->ai_next;
      }

      freeaddrinfo(host_info);

      return answer;
    }

    // ---------------------------------------------------------------------------
    TSocket accept(TSocket server) {
      dbg("FD %d is accepting connections\n", server);

      while (server) {
        struct sockaddr_storage sa;
        socklen_t sa_sz = sizeof(sa);
        auto rc = sys_accept(server.s, &sa, &sa_sz);
        if (rc == TSocket::invalid) {
          int sys_err = sys_errno;
          if (sys_err == SYS_ERR_WOULD_BLOCK) {
            dbg("FD %d goes to sleep waiting for a connection\n", server);
            TWatchedEvent we(server.s, EVT_SOCKET_IO_CAN_READ);
            wait(&we, 1);
            continue;
          }
          dbg("FD %d accept failed (%08x)\n", server, sys_err); // , strerror(sys_err) );
                                                            // Other types of errors
          return TSocket::invalid;
        }
        dbg("FD %d has accepted new client %d\n", server, rc);
        auto new_client = rc;
        setNonBlocking( new_client );
        return new_client;
      }
      return TSocket::invalid;
    }

    // ---------------------------------------------------------------------------
    TSocket listen(const char* bind_addr, int port, int af) {

      TSocket s;
      char port_str[8];
      struct addrinfo hints, *servinfo, *p;

      snprintf(port_str, sizeof(port_str) - 1, "%d", port);
      memset(&hints, 0, sizeof(hints));
      hints.ai_family = af;
      hints.ai_socktype = SOCK_STREAM;
      hints.ai_flags = AI_PASSIVE;    // No effect if bindaddr != NULL

      int rc;
      if ((rc = getaddrinfo(bind_addr, port_str, &hints, &servinfo)) != 0)
        return TSocket::invalid;

      for (p = servinfo; p != NULL; p = p->ai_next) {

        if ((s.s = sys_socket(p->ai_family, p->ai_socktype, p->ai_protocol, 0)) == -1)
          continue;

        if (sys_bind(s.s, p->ai_addr, p->ai_addrlen) >= 0) {
          if (sys_listen(s.s, 5) >= 0)
            break;
        }

        sys_close(s.s);
      }

      if (!p)
        return false;

      freeaddrinfo(servinfo);
      setNonBlocking(s);

      return s;
    }

    // ---------------------------------------------------------------------------
    bool close(TSocket sock) {
      sys_close(sock.s);
      return true;
    }

    // ---------------------------------------------------------------------------
    bool send(TSocket sock, const void* src_buffer, size_t bytes_to_send) {
      assert(bytes_to_send > 0);
      size_t total_bytes_sent = 0;
      while (sock) {
        assert(bytes_to_send > total_bytes_sent);
        auto bytes_sent = sys_send(sock.s, ((const char*)src_buffer) + total_bytes_sent, (int)(bytes_to_send - total_bytes_sent), 0);
        if (bytes_sent == -1) {
          if (errno == SYS_ERR_WOULD_BLOCK) {
            TWatchedEvent we(sock.s, EVT_SOCKET_IO_CAN_WRITE);
            wait(&we, 1);
          }
          else
            break;
        }
        else {
          //dbg("FD %d sent %ld bytes\n", fd, bytes_sent);
          total_bytes_sent += bytes_sent;
          if (total_bytes_sent == bytes_to_send)
            return true;
        }
      }
      return false;
    }

    // ---------------------------------------------------------------------------
    bool recv(TSocket sock, void* dest_buffer, size_t bytes_to_read) {
      assert(bytes_to_read > 0);
      size_t total_bytes_read = 0;
      while (sock) {
        assert(bytes_to_read > total_bytes_read);
        auto new_bytes_read = sys_recv(sock.s, (char*)dest_buffer, (int)(bytes_to_read - total_bytes_read), 0);
        if (new_bytes_read == -1) {
          int err = sys_errno;
          if (err == SYS_ERR_WOULD_BLOCK) {
            TWatchedEvent we(sock.s, EVT_SOCKET_IO_CAN_READ);
            wait(&we, 1);
          }
          else
            break;
        }
        else if (new_bytes_read == 0) {
          break;
        }
        else {
          total_bytes_read += new_bytes_read;
          if (total_bytes_read == bytes_to_read)
            return true;
        }
      }
      return false;
    }

    // ---------------------------------------------------------------------------
    int recvUpTo(TSocket sock, void* dest_buffer, size_t bytes_to_read) {
      while (sock) {
        auto new_bytes_read = sys_recv(sock.s, (char*)dest_buffer, (int)(bytes_to_read), 0);
        if (new_bytes_read == -1) {
          int err = sys_errno;
          if (err == SYS_ERR_WOULD_BLOCK) {
            TWatchedEvent we(sock.s, EVT_SOCKET_IO_CAN_READ);
            wait(&we, 1);
          }
          else
            break;
        }
        else {
          return new_bytes_read;
        }
      }
      return -1;
    }

  }

}



