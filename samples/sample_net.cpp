#include <cstdarg>
#include <cstdio>
#include <vector>
#include "coroutines/coroutines.h"
#include "coroutines/io_channel.h"
#include "sample.h"

using namespace Coroutines;
using Coroutines::wait;

int port = 8081;

// ----------------------------------------------------------
void sample_net_echo() {
  TSimpleDemo demo("sample_net_echo");
  auto co_s = start([]() {
    auto server = Net::listen("127.0.0.1", port, AF_INET);
    if (!server)
      return;
    while (true) {
      auto client = Net::accept(server);
      if (!client)
        break;
      char buf[256];
      int nbytes = Net::recvUpTo(client, buf, sizeof(buf));
      Net::send(client, buf, nbytes);
      Net::close(client);
      break;
    }
    Net::close(server);
  });

  auto co_c = start([]() {
    // Fake pause
    Time::sleep(10 * Time::MilliSecond);
    const char* addr = "127.0.0.1";
    Net::TSocket client = Net::connect(addr, port);
    if (!client)
      return;
    Net::send(client, "hello");
    char buf[256];
    int nbytes = Net::recvUpTo(client, buf, sizeof(buf));
    dbg("Server answer is: %s\n", buf);
    Net::close(client);
  });

}

// ---------------------------------------------------------
static void runServer(int max_clients) {

  // Wait some time before starting the server
  dbg("Server: Doing a small pause of 50ms\n");
  //Time::sleep(50 * Time::MilliSecond);
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
    auto co_client = start([client]() {
      int n = 0;
      while (true) {
        dbg("Server: Waiting for client to send an int\n");
        if (!Net::recv(client, n))
          break;
        n++;
        dbg("Server: Sending answer %d to client\n", n);
        if (!Net::send(client, n))
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
    if (!Net::send(client, id)) {
      dbg("Client: Send failed\n");
      return;
    }
    dbg("Client: Receiving...\n");
    if (!Net::recv(client, id)) {
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

// ----------------------------------------------------------
void sample_net() {
  //sample_net_echo();
  sample_net_multiples();
}
