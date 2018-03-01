#include "coroutines/io_events.h"
#include "coroutines/io_channel.h"

//extern void dbg(const char *fmt, ...);
#define dbg(...)

#ifdef _WIN32

#pragma comment(lib, "Ws2_32.lib")
#define sys_socket(x,y,z,port)	::socket( x, y, z )
#define sys_connect(id,addr,sz) ::connect( id, (const sockaddr*) addr, sz )
#define sys_send                ::send
#define sys_recv                ::recv
#define sys_errno               ::WSAGetLastError()
//#define sys_sendto(sock_id, data, data_bytes, flags, target_addr, sizeof_target_addr )        ::sendto( sock_id, (const char *)data, (int)data_bytes, flags, (const sockaddr *) target_addr, sizeof_target_addr )
//#define sys_recvfrom(sock_id, data, data_bytes, flags, sender_addr, sizeof_sender_addr )      ::recvfrom( sock_id, (char *) data, (int)data_bytes, flags, (sockaddr*) sender_addr, sizeof_sender_addr )
#define sys_close               ::closesocket
#define sys_bind(id,addr,sz)    ::bind(id, (const sockaddr *) addr, sz )
//#define sys_fcntl         ::ioctlsocket
#define sys_accept(id,addr,sz)  ::accept( id, (sockaddr*) addr, sz )
#define sys_listen              ::listen
//#define sys_seterror(bytes,sys_errno)  

#define SYS_ERR_WOULD_BLOCK      WSAEWOULDBLOCK

#else

#error need defintion for SYS_ERR_WOULD_BLOCK

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
    return rc == 0;
  }

  bool CIOChannel::listen(const TNetAddress& serving_addr) {
    if (isValid())
      return false;

    auto new_fd = sys_socket(AF_INET, SOCK_STREAM, 0, 0);
    if (new_fd < 0)
      return false;
    fd = new_fd;

    if (sys_bind(fd, &serving_addr, sizeof(serving_addr)) < 0)
      return false;

    if (sys_listen(fd, 5) < 0)
      return false;

    setNonBlocking();

    return true;
  }

  CIOChannel CIOChannel::accept() {
    dbg("FD %d is accepting connections\n", fd);

    while (isValid()) {
      TNetAddress remote_client_addr;
      int remote_addr_sz = sizeof(remote_client_addr);
      int rc = sys_accept(fd, &remote_client_addr.addr, &remote_addr_sz);
      if (rc < 0) {
        int sys_err = sys_errno;
        if (sys_err == SYS_ERR_WOULD_BLOCK) {
          dbg("FD %d goes to sleep waiting for a connection\n", fd);
          TWatchedEvent we(fd, EVT_SOCKET_IO_CAN_READ);
          wait(&we, 1);
          continue;
        }
        dbg("FD %d accept failed\n", fd);
        // Other types of errors
        return CIOChannel();
      }
      dbg("FD %d has accepted new client %d\n", fd, rc);
      CIOChannel new_client;
      new_client.fd = rc;
      return new_client;
    }
    return CIOChannel();
  }

  bool CIOChannel::connect(const TNetAddress &remote_server, int timeout_sec) {

    auto new_fd = sys_socket(AF_INET, SOCK_STREAM, 0, 0);
    if (new_fd < 0)
      return false;
    fd = new_fd;

    setNonBlocking();

    while (isValid()) {
      int rc = sys_connect(fd, &remote_server.addr, sizeof(remote_server));
      if (rc < 0) {
        int sys_err = sys_errno;
        if (sys_err == SYS_ERR_WOULD_BLOCK) {
          dbg("FD %d waiting to connect\n", fd);
          TWatchedEvent we(fd, EVT_SOCKET_IO_CAN_WRITE);
          int n = wait(&we, 1);
          if (n == 0)
            break;
          continue;
        }
      }
      else {
        break;
      }
    }
    dbg("FD %d connected\n", fd);
    return isValid();
  }

  void CIOChannel::close() {
    if (isValid()) {
      dbg("FD %d closed\n", fd);
      sys_close(fd);
      fd = invalid_socket_id;
    }
  }

  bool CIOChannel::recv(void* dest_buffer, size_t bytes_to_read) {
    size_t total_bytes_read = 0;
    while (isValid()) {
      auto new_bytes_read = sys_recv(fd, (char*)dest_buffer, (int)( bytes_to_read - total_bytes_read ), 0);
      if (new_bytes_read == -1) {
        int err = sys_errno;
        if (err == SYS_ERR_WOULD_BLOCK) {
          TWatchedEvent we(fd, EVT_SOCKET_IO_CAN_READ);
          dbg("FD %d waiting to read\n", fd);
          wait(&we, 1);
        }
        else
          break;
      }
      else if (new_bytes_read == 0) {
        break;
      }
      else {
        dbg("FD %d read %ld bytes\n", fd, new_bytes_read);
        total_bytes_read += new_bytes_read;
        if(total_bytes_read == bytes_to_read)
          return true;
      }
    }
    return false;
  }

  int CIOChannel::recvUpTo(void* dest_buffer, size_t bytes_to_read) {
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

  bool CIOChannel::send(const void* src_buffer, size_t bytes_to_send) {
    size_t total_bytes_sent = 0;
    while (isValid()) {
      auto bytes_sent = sys_send(fd, ((const char*)src_buffer) + total_bytes_sent, (int) ( bytes_to_send - total_bytes_sent ), 0);
      if (bytes_sent == -1) {
        if (errno == SYS_ERR_WOULD_BLOCK) {
          dbg("FD %d waiting to write\n", fd);
          TWatchedEvent we(fd, EVT_SOCKET_IO_CAN_WRITE);
          wait(&we, 1);
        }
        else
          break;
      }
      else {
        dbg("FD %d sent %ld bytes\n", fd, bytes_sent);
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
#if defined( NET_ADDR_HAS_SIN_LEN_MEMBER )
  addr.sin_len = sizeof(addr);
#endif
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


