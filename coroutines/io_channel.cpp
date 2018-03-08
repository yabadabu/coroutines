#define _CRT_SECURE_NO_WARNINGS
#include "coroutines/io_events.h"
#include "coroutines/io_channel.h"

extern void dbg(const char *fmt, ...);
//#define dbg(...)

#ifdef _WIN32

#pragma comment(lib, "Ws2_32.lib")
#define sys_socket(x,y,z,port)	::socket( x, y, z )
#define sys_connect(id,addr,sz) ::connect( id, (const sockaddr*) addr, sz )
#define sys_send                ::send
#define sys_recv                ::recv
#define sys_errno               ::WSAGetLastError()
#define sys_close               ::closesocket
#define sys_bind(id,addr,sz)    ::bind(id, (const sockaddr *) addr, sz )
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

  bool CIOChannel::setNonBlocking() {
    // set non-blocking
#if defined(O_NONBLOCK)
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1)
      flags = 0;
    auto rc = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#else
    u_long iMode = 1;
    auto rc = ioctlsocket(fd, FIONBIO, &iMode);
#endif
    if (rc != 0)
      dbg("Failed to set socket %d as non-blocking\n", fd);
    return rc == 0;
  }

  // ---------------------------------------------------------------------------
  int CIOChannel::getSocketError() {
    // Confirm we are really connected by checking the socket error
    int sock_err = 0;
    socklen_t sock_err_sz = sizeof(sock_err);
    int rc = getsockopt(fd, SOL_SOCKET, SO_ERROR, (char*)&sock_err, &sock_err_sz);
    return sock_err;
  }


  // ---------------------------------------------------------------------------
  bool CIOChannel::listen(const char* bind_addr, int port, int af) {
    if (isValid())
      return false;

    int s = -1, rv;
    char _port[8];
    struct addrinfo hints, *servinfo, *p;

    snprintf(_port, 6, "%d", port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = af;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;    // No effect if bindaddr != NULL

    if ((rv = getaddrinfo(bind_addr, _port, &hints, &servinfo)) != 0)
      return false;

    for (p = servinfo; p != NULL; p = p->ai_next) {

      if ((s = sys_socket(p->ai_family, p->ai_socktype, p->ai_protocol, 0)) == -1)
        continue;

      if (sys_bind(s, p->ai_addr, p->ai_addrlen) >= 0) {
        if (sys_listen(s, 5) >= 0)
          break;
      }

      sys_close(s);
    }

    if (!p)
      return false;

    freeaddrinfo(servinfo);
    fd = s;
    setNonBlocking();

    return true;
  }

  // ---------------------------------------------------------------------------
  CIOChannel CIOChannel::accept() {
    dbg("FD %d is accepting connections\n", fd);

    while (isValid()) {
      struct sockaddr_storage sa;
      socklen_t sa_sz = sizeof(sa);
      int rc = sys_accept(fd, &sa, &sa_sz);
      if (rc < 0) {
        int sys_err = sys_errno;
        if (sys_err == SYS_ERR_WOULD_BLOCK) {
          dbg("FD %d goes to sleep waiting for a connection\n", fd);
          TWatchedEvent we(fd, EVT_SOCKET_IO_CAN_READ);
          wait(&we, 1);
          continue;
        }
        dbg("FD %d accept failed (%08x)\n", fd, sys_err); // , strerror(sys_err) );
        // Other types of errors
        return CIOChannel();
      }
      dbg("FD %d has accepted new client %d\n", fd, rc);
      CIOChannel new_client;
      new_client.fd = rc;
      new_client.setNonBlocking();
      return new_client;
    }
    return CIOChannel();
  }

  // ---------------------------------------------------------------------------
  bool CIOChannel::connect(const char* addr, int port, int timeout_sec) {

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
      return false;

    // For each alternative proposed by getaddrinfo....
    struct addrinfo* target_addr = host_info;

    while (target_addr) {

      // Create a socket of the family suggested
      auto new_fd = sys_socket(target_addr->ai_family, target_addr->ai_socktype, target_addr->ai_protocol, 0);
      if (new_fd < 0)
        return false;

      fd = new_fd;
      if (setNonBlocking()) {

        // Now connect to that address
        int rc = sys_connect(fd, target_addr->ai_addr, target_addr->ai_addrlen);

        if (rc < 0) {
          int sys_err = sys_errno;
          if (sys_err == SYS_ERR_CONN_IN_PROGRESS) {
            dbg("FD %d waiting to connect\n", fd);
            TWatchedEvent we(fd, EVT_SOCKET_IO_CAN_WRITE);
            int n = wait(&we, 1);
            if (n == 0) {

              // Confirm we are really connected by checking the socket error
              int sock_err = getSocketError();

              // All ok, no errors
              if (sock_err == 0)
                break;

              // The expected error in this case is Conn Refused when there is no server
              // in the remote address. Other erros, I prefer to report them
              if (sock_err != ECONNREFUSED)
                dbg("connect.failed getsockopt( %d ) (err=%08x %s)\n", fd, sock_err, strerror( sock_err ));
            }
          }
        }
      }

      sys_close(fd);

      // Try next candidate
      target_addr = target_addr->ai_next;
    }

    freeaddrinfo(host_info);

    return isValid();
  }

  // ---------------------------------------------------------------------------
  void CIOChannel::close() {
    if (isValid()) {
      dbg("FD %d closed\n", fd);
      sys_close(fd);
      fd = invalid_socket_id;

      // Remove from entries...
    }
  }

  // ---------------------------------------------------------------------------
  bool CIOChannel::recv(void* dest_buffer, size_t bytes_to_read) const {
    assert(bytes_to_read > 0);
    size_t total_bytes_read = 0;
    while (isValid()) {
      assert(bytes_to_read > total_bytes_read);
      auto new_bytes_read = sys_recv(fd, (char*)dest_buffer, (int)(bytes_to_read - total_bytes_read), 0);
      if (new_bytes_read == -1) {
        int err = sys_errno;
        if (err == SYS_ERR_WOULD_BLOCK) {
          TWatchedEvent we(fd, EVT_SOCKET_IO_CAN_READ);
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
  int CIOChannel::recvUpTo(void* dest_buffer, size_t bytes_to_read) const {
    while (isValid()) {
      auto new_bytes_read = sys_recv(fd, (char*)dest_buffer, (int)(bytes_to_read), 0);
      if (new_bytes_read == -1) {
        int err = sys_errno;
        if (err == SYS_ERR_WOULD_BLOCK) {
          TWatchedEvent we(fd, EVT_SOCKET_IO_CAN_READ);
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

  // ---------------------------------------------------------------------------
  bool CIOChannel::send(const void* src_buffer, size_t bytes_to_send) const {
    assert(bytes_to_send > 0);
    size_t total_bytes_sent = 0;
    while (isValid()) {
      assert(bytes_to_send > total_bytes_sent);
      auto bytes_sent = sys_send(fd, ((const char*)src_buffer) + total_bytes_sent, (int)(bytes_to_send - total_bytes_sent), 0);
      if (bytes_sent == -1) {
        if (errno == SYS_ERR_WOULD_BLOCK) {
          TWatchedEvent we(fd, EVT_SOCKET_IO_CAN_WRITE);
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
}

// -----------------------------------------------
// port and vport are in host format
void TNetAddress::from(int port, unsigned ip4_in_host_format) {
  assert(port > 0 && port < 65535);
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(ip4_in_host_format);
  addr.sin_port = htons(port);
}

void TNetAddress::setPort(unsigned short new_app_port) {
  addr.sin_port = htons(new_app_port);
}

void TNetAddress::fromAnyAddress(int port) {
  from(port, INADDR_ANY);
}

bool TNetAddress::fromStr(const char *addr_str, int port) {
  fromAnyAddress(port);
  return inet_pton(AF_INET, addr_str, &addr.sin_addr) == 1;
}


