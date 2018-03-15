#include <cstdarg>
#include <cstdio>
#include <vector>
#include "sample.h"

using namespace Coroutines;

namespace Coroutines {

  namespace Time {

    void sleep(TTimeDelta ms_to_sleep) {
      wait(nullptr, 0, ms_to_sleep);
    }

    TTimeDelta milliseconds(int num_ms) {
      return TTimeDelta(num_ms);
    }

    TWatchedEvent after(TTimeDelta ms_to_sleep) {
      TWatchedEvent we( ms_to_sleep );
      return we;
    }

  }

}

typedef TChannel<const char*> StrChan;

void operator<=(const char* &p, StrChan& c) {
  c.pull(p);
}
void operator<=(StrChan& c, const char* &p) {
  c.push(p);
}


void test_concurrency() {

  auto sc = new StrChan();

  auto co1 = start(std::bind([sc]( const char* label ){
    while (true) {
      sc->push(label);
      Time::sleep(Time::milliseconds(1000));
    }
  }, "john"));

  auto co2 = start([sc]() {
    for (int i = 0; i < 5; ++i) {
      const char* msg;
      if (sc->pull(msg))
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
    while (true) {
      const char* msg;
      if (!a->pull(msg))
        break;
      c->push(msg);
    }
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



void test_select() {
  TSimpleDemo demo("test_select");

  auto b1 = boring("john");
  auto b2 = boring("peter");
  auto b = fanInSelect(b1, b2);
  auto co2 = readChannel(b, 10);

  while (isHandle(co2))
    executeActives();

  b->close();
  b1->close();
  b2->close();
}

// ----------------------------------------------------------
void sample_go() {
//  test_concurrency();
//  test_borings();
//  test_fanIn();
  test_select();
}
