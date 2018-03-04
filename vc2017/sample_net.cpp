#define _CRT_SECURE_NO_WARNINGS
#include <cstdarg>
#include <cstdio>
#include <vector>
#include "coroutines/coroutines.h"
#include "coroutines/io_channel.h"

using namespace Coroutines;

extern void dbg(const char *fmt, ...);
extern void runUntilAllCoroutinesEnd();

int port = 8081;

// ---------------------------------------------------------
static void runServer() {

  TNetAddress listenning_addr;
  listenning_addr.fromAnyAddress(port);

  // Wait some time before starting the server
  dbg("Server: Doing a small pause of 1s\n");
  wait(nullptr, 0, 1000);
  dbg("Server: Pause complete. listen..\n");

  CIOChannel server;
  if (!server.listen(listenning_addr)) {
    dbg("Server: Failed to start the server at port %d.\n", port);
    return;
  }
  dbg("Server: Accepting connections.\n");

  int max_clients = 2;
  int nclients = 0;
  while (nclients < max_clients) {

    CIOChannel client = server.accept();
    if (!client.isValid()) {
      dbg("Server: accept failed\n");
      return;
    }

    dbg("Server: New client connected\n");
    auto co_client = start(std::bind( [](CIOChannel client) {
      int n = 0;
      while (true) {
        dbg("Server: Waiting for client\n");
        if (!client.recv(n))
          break;
        n++;
        dbg("Server: Sending answer %d to client\n", n);
        if (!client.send(n))
          break;
      }
      dbg("Server: Client has been disconnected. Loops=%d\n", n);
      client.close();
    }, client ));

    ++nclients;
  }

  dbg("Server: Closing\n");
  server.close();

}

// ---------------------------------------------------------
// Connects, then send's an id, and recv another.
static void runClient(int max_id) {

  TNetAddress addr;
  addr.fromStr("127.0.0.1", port);
  dbg("Client: Connecting to server\n");

  CIOChannel client;
  if (!client.connect(addr, 1000)) {
    dbg("Client: Can't connect to server.\n");
    return;
  }
  dbg("Client: Connected to server.\n");

  int id = 0;
  while (id < max_id) {
    dbg("Client: Sending %d / %d\n", id, max_id);
    if (!client.send(id)) {
      dbg("Client: Send failed\n");
      return;
    }
    dbg("Client: Receiving...\n");
    if (!client.recv(id)) {
      dbg("Client: Recv failed\n");
      return;
    }
    dbg("Client: Received %d / %d\n", id, max_id);
  }
  dbg("Client: Exiting after %d / %d loops\n", id, max_id);
  client.close();
}


// ----------------------------------------------------------
void sample_net() {

#ifdef _WIN32
  WSADATA wsaData;
  int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
  
  auto co_s = start( &runServer );
  auto co_c1 = start([]() { runClient(1); });
  auto co_c2 = start([]() { runClient(2); });

  dbg( "Waiting from main to finish...\n");
  runUntilAllCoroutinesEnd();
}
