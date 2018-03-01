#define _CRT_SECURE_NO_WARNINGS
#include <cstdarg>
#include <cstdio>
#include <vector>
#include "coroutines/coroutines.h"
#include "coroutines/io_channel.h"

using namespace Coroutines;

extern void dbg(const char *fmt, ...);
int port = 8081;

// ---------------------------------------------------------
static void runServer() {

  TNetAddress listenning_addr;
  listenning_addr.fromAnyAddress(port);

  CIOChannel server;
  if (!server.listen(listenning_addr)) {
    dbg("Server: Failed to start the server at port %d.\n", port);
    return;
  }
  dbg("Server: Accepting connections.\n");

  int max_clients = 1;
  int nclients = 0;
  while (nclients < max_clients) {
    CIOChannel client = server.accept();
    if (!client.isValid()) {
      dbg("Server: accept failed\n");
      return;
    }

    dbg("Server: New client connected\n");
    {
      int id = 0;
      while (true) {
        //dbg("Server: Waiting for client\n");
        if (!client.recv(id))
          break;
        id++;
        //dbg("Server: Sending answer %d to client\n", id);
        if (!client.send(id))
          break;
      }
      dbg("Server: Client has been disconnected %d\n", id);
      client.close();
    }

    ++nclients;
  }

}

// ---------------------------------------------------------
static void runClient(int max_id) {

  TNetAddress addr;
  addr.fromStr("127.0.0.1", port);

  CIOChannel client;
  if (!client.connect(addr, 1000))
    return;
  dbg("Client: Connected to server.\n");

  int id = 0;
  while (id < max_id) {
    //dbg("Client: Sending %d\n", id);
    if (!client.send(id))
      return;
    //dbg("Client: Receiving...\n");
    if (!client.recv(id))
      return;
    //dbg("Client: Received %d\n", id);
  }
  dbg("Client: Exiting after %d loops\n", id);
  client.close();
}


// ----------------------------------------------------------
void test_app3() {

  WSADATA wsaData;
  int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);

  auto co_s = start( &runServer );
  auto co_c1 = start([]() { runClient(10000); });

  while (true) {
    Coroutines::updateCurrentTime(1);
    if (!Coroutines::executeActives())
      break;
  }
  dbg("done\n");
}
