#include <cstdarg>
#include <cstdio>
#include <vector>
#include "sample.h"
#include "coroutines/choose.h"

using namespace Coroutines;

typedef TChannel<const char*> StrChan;

// -----------------------------------------------------------
// Dangerous hack to write something similar to go
// TObj obj;
// while( channel >> obj ) {
//   obj >> out_channel;
// }

template< typename T >
bool operator<<(T& p, TChannel<T>& c) {
  return c.pull(p);
}

template< typename T >
bool operator<<(TChannel<T>& c, T& p) {
  return c.push(p);
}

template< typename T >
bool operator>>(T& p, TChannel<T>& c) {
  return c.push(p);
}

template< typename T >
bool operator>>(TChannel<T>& c, T& p) {
  return c.pull(p);
}

// -----------------------------------------------------------
void test_concurrency() {

  auto sc = new StrChan();

  auto co1 = start(std::bind([sc]( const char* label ){
    while (true) {
      sc->push(label);
      //*sc << label;
      Time::sleep(Time::milliseconds(1000));
    }
  }, "john"));

  auto co2 = start([sc]() {
    for (int i = 0; i < 5; ++i) {
      const char* msg;
      if( msg << *sc )
        dbg("You are %s\n", msg);
    }
    dbg("Bye\n");
  });
  
  // Only wait for co2
  while (isHandle(co2))
    executeActives();

  exitCo(co1);
}

// ---------------------------------------------------------
StrChan* boring(const char* label, int min_time = 0) {
  auto sc = new StrChan();
  start([sc, label, min_time]() {
    while (true) {
      if (!sc->push(label))
        break;
      Time::sleep(Time::milliseconds(min_time + (rand() % 1000)));
    }
  });
  return sc;
}

void test_borings() {
  TSimpleDemo demo("test_borings");

  auto b1 = boring("john");
  auto b2 = boring("peter");

  auto co2 = start([b1, b2]() {
    for (int i = 0; i < 5; ++i) {
      const char* msg;
      b1->pull(msg);
      dbg("%s\n", msg);
      b2->pull(msg);
      dbg("%s\n", msg);
    }
    dbg("Bye\n");
  });

  // Only wait for co2
  while (isHandle(co2))
    executeActives();
  b1->close();
  b2->close();
  dbg("Done\n");
}

// -------------------------------------------------
THandle readChannel(StrChan* c, int n) {
  return start([c, n]() {
    for (int i = 0; i < n; ++i) {
      const char* msg = nullptr;
      if (!c->pull(msg))
        break;
      dbg("%s\n", msg);
    }
    dbg("Bye\n");
  });
}


StrChan* fanIn(StrChan* a, StrChan* b) {
  StrChan* c = new StrChan;

  start([c, a]() {
    const char* msg;
    while ((*a) >> msg)
      msg >> (*c);
  });

  start([c, b]() {
    while (true) {
      const char* msg;
      if (!b->pull(msg))
        break;
      c->push(msg);
    }
  });
  return c;
}


void test_fanIn() {
  TSimpleDemo demo("test_fanIn");

  auto b1 = boring("john");
  auto b2 = boring("peter");
  auto b = fanIn(b1, b2);
  auto co2 = readChannel(b, 10);

  while (isHandle(co2))
    executeActives();

  b->close();
  b1->close();
  b2->close();

  dbg("Done\n");
}

// -------------------------------------------
StrChan* fanInSelect(StrChan* a, StrChan* b) {
  StrChan* c = new StrChan;

  start([c, a, b]() {
    while (true) {

      // Wait until we can 'read' from any of those channels
      const char* msg;
      TWatchedEvent we[3] = { 
        TWatchedEvent(a, msg, eEventType::EVT_CHANNEL_CAN_PULL ), 
        TWatchedEvent(b, msg, eEventType::EVT_CHANNEL_CAN_PULL ), 
        Time::after( 400 )
      };
      int n = wait(we, 3);
      if (n == 2 || n == -1) {
        dbg("Too slows..\n");
        c->close();
        break;
      }
      StrChan* ic = (n == 0) ? a : b;

      if (!ic->pull(msg))
        break;

      c->push(msg);
    }
  });

  return c;
}
// --------------------------------------------------------------
// User code
StrChan* fanInSelect2(StrChan* a, StrChan* b) {
  StrChan* c = new StrChan;

  start([c, a, b]() {
    while (true) {
      
      // Wait until we can 'read' from any of those channels
      int n = choose(
        ifCanPull(a, [c](const char* msg) {
          dbg("Hi, I'm A and pulled data %s\n", msg);
          c->push(msg);
        }),
        ifCanPull(b, [c](auto msg) {
          dbg("Hi, I'm B and pulled data %s\n", msg);
          c->push(msg);
        }),
        ifTimeout(400, []() {
          dbg("Timeout waiting for a or b\n");
        })
      );

      if (n == 2 || n == -1) {
        dbg("Too slows..\n");
        c->close();
        break;
      }
    }

  });

  return c;
}

void test_select() {
  TSimpleDemo demo("test_select");

  auto b1 = boring("john");
  auto b2 = boring("peter");
  auto b = fanInSelect2(b1, b2);
  auto co2 = readChannel(b, 10);

  while (isHandle(co2))
    executeActives();

  b->close();
  b1->close();
  b2->close();
}

// ----------------------------------------------------------
void sample_go() {
///  test_concurrency();
//  test_borings();
//  test_fanIn();
  test_select();
}


/*

int ch = newChannel<int>();
int ch = newChannel<int>(10);
push( ch, 2 );
push<float>( ch, 2.f );
int id;
pull( ch, id );
pull( ch, &id, sizeof(int));
pull<float>( ch, fdata );
closeChannel(ch);
deleteChannel( ch );

int ch = newTcpConnection(ip_addr)
push( ch, 2 );
pull( ch, id );
int nbytes = pullUpTo( ch, addr, max_bytes_to_pull );
deleteChannel( ch );

int ch_s = newTcpServer(ip_addr);
int ch_c = newTcpAccept( ch_s );
...
deleteChannel( ch_s );

// Time functions

*/
