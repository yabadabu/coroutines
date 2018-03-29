#include <cstdarg>
#include <cstdio>
#include <vector>
#include "coroutines/coroutines.h"
#include "coroutines/io_channel.h"
#include "sample.h"

using namespace Coroutines;
using namespace Coroutines::Time;
using Coroutines::wait;

int port = 8081;

// ----------------------------------------------------------
void echoClient() {
  const char* addr = "127.0.0.1";
  Net::TSocket client = Net::connect(addr, port);
  if (!client)
    return;
  // Will send 5+1 bytes
  client << "Hello";
  char buf[256];
  int nbytes = Net::recvUpTo(client, buf, sizeof(buf));
  dbg("Server answer is: %s\n", buf);
  Net::close(client);
}

void echoServer(Net::TSocket server) {
  auto client = Net::accept(server);
  if (!client)
    return;
  char buf[256];
  int nbytes = Net::recvUpTo(client, buf, sizeof(buf));
  Net::send(client, buf, nbytes);
  Net::close(client);
}

// ----------------------------------------------------------
void sample_net_echo() {
  TSimpleDemo demo("sample_net_echo");
  auto co_s = start([]() {
    auto server = Net::listen("127.0.0.1", port, AF_INET);
    if (!server)
      return;
    while (true) {
      echoServer(server);
      break;
    }
    Net::close(server);
  });

  auto co_c = start(echoClient);
}

// ---------------------------------------------------------
static void runServer(int max_clients) {

  // Wait some time before starting the server
  dbg("Server: Doing a small pause of 50ms\n");
  Time::sleep(50 * Time::MilliSecond);
  dbg("Server: Pause complete. listen..\n");

  auto server = Net::listen("127.0.0.1", port, AF_INET);
  if( !server ) {
    dbg("Server: Failed to start the server at port %d.\n", port);
    return;
  }
  dbg("Server: Accepting %d connections.\n", max_clients);

  int nclients = 0;
  while (nclients < max_clients) {

    auto client = Net::accept(server);
    if (!client) {
      dbg("Server: accept failed\n");
      continue;
    }

    dbg("Server: New client connected\n");
    // Just be sure not to use start([&](){ ... } as 
    // the ref value will be garbage in the next while loop
    auto co_client = start([client]() {
      int n = 0;
      while (true) {
        dbg("Server: Waiting for client to send an int\n");
        if(!(n << client))
          break;
        n++;
        dbg("Server: Sending answer %d to client\n", n);
        if(!(client << n))
          break;
      }
      dbg("Server: Client has been disconnected. Loops=%d\n", n);
      Net::close(client);
    });

    ++nclients;
  }

  dbg("Server: Closing server socket\n");
  Net::close( server );
}

// ---------------------------------------------------------
// Connects, then send's an id, and recv another.
static void runClient(int max_id) {

  const char* addr = "127.0.0.1";
  //addr = "::1";
  dbg("Client: Connecting to server %s\n", addr);

  // Time::sleep(1000 * Time::MilliSecond);

  auto client = Net::connect(addr, port);
  if (!client) {
    dbg("Client: Can't connect to server %s.\n", addr);
    return;
  }
  dbg("Client: Connected to server %s.\n", addr);

  int id = 0;
  while (id < max_id) {
    dbg("Client: Sending %d / %d\n", id, max_id);
    if (!(client << id)) {
      dbg("Client: Send failed\n");
      return;
    }
    dbg("Client: Receiving...\n");
    if (!(id << client)) {
      dbg("Client: Recv failed\n");
      return;
    }
    dbg("Client: Received %d / %d\n", id, max_id);
  }
  dbg("Client: Exiting after %d / %d loops\n", id, max_id);
  Net::close( client );
}

void sample_net_multiples() {
  TSimpleDemo demo("sample_net_multiples");
  auto co_s = start([]() { runServer(2); });
  auto co_c1 = start([]() { runClient(1); });
  auto co_c2 = start([]() { runClient(2); });
}


// -------------------------------------------------------------
struct ifCanRead {
  Net::TSocket                 sock;
  std::function<void(Net::TSocket)>    cb;
  ifCanRead(Net::TSocket new_sock, std::function< void(Net::TSocket) >&& new_cb)
    : sock(new_sock)
    , cb(new_cb)
  { }
  void declareEvent(TWatchedEvent* we) {
    *we = TWatchedEvent(sock.s, EVT_SOCKET_IO_CAN_READ);
  }
  bool run() {
    cb(sock);
    return true;
  }
};


// Helper function to deduce the arguments in a fn, not as the ctor args
struct ifTimer {
  TTimeHandle                     handle;
  std::function<void(TTimeStamp)> cb;
  ifTimer(TTimeHandle new_handle, std::function< void(TTimeStamp ts) >&& new_cb)
    : handle(new_handle)
    , cb(new_cb)
  { }
  void declareEvent(TWatchedEvent* we) {
    *we = TWatchedEvent( handle.timeForNextEvent() );
  }
  bool run() {
    TTimeStamp ts;
    if (ts << handle) {
      cb(ts);
      return true;
    }
    return false;
  }
}; 

// ----------------------------------------------------------
void sample_net_choose() {
  TSimpleDemo demo("sample_net_choose");
  
  StrChan chan = StrChan::create();

  auto co_s = start([chan]() {
    auto server = Net::listen("127.0.0.1", port, AF_INET);
    if (!server)
      return;

    auto ticker = every(Second);

    while (true) {

      int idx = choose(
        ifCanRead(server, [](Net::TSocket server) {
          // This could block inside...
          // Start a new coroutine if you don't want 
          echoServer(server);
        }),
        ifCanPull(chan, [](const char* v) {
          dbg("Channel %s\n", v);
        }),
        ifTimer(ticker, [](TTimeStamp ts) {
          dbg("Ticker...\n");
        })
        //, ifTimeout(950 * Time::MilliSecond, []() {
        //  dbg("Server tick...\n");
        //})
      );
      if (idx == -1)
        break;
    }
    Net::close(server);
  });
  
  auto co_c = start([] {
    Time::sleep(4500 * Time::MilliSecond);
    echoClient();
  });

  auto co_d = start([chan] {
    const char* names[] = { "john","peter","foo","bar" }; // , "laia", "luke" };
    for (auto name : names) {
      sleep(750 * Time::MilliSecond);
      chan << name;
    }
    close(chan);
    dbg("All names pushed\n");
  });

}

// ----------------------------------------------------------
void sample_net() {
  sample_net_echo();
  //sample_net_multiples();
  sample_net_choose();
}
