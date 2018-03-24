#include <cstdarg>
#include <cstdio>
#include <vector>
#include "sample.h"
#include "coroutines/choose.h"

using namespace Coroutines;

typedef TTypedChannel<const char*> StrChan;

// -----------------------------------------------------------
void test_concurrency() {

  auto sc = StrChan::create();

  auto co1 = start(std::bind([sc]( const char* label ){
    while (true) {
      sc << label;
      Time::sleep(Time::Second);
    }
  }, "john"));

  auto co2 = start([sc]() {
    for (int i = 0; i < 5; ++i) {
      const char* msg;
      if( msg << sc )
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
StrChan boring(const char* label, TTimeDelta min_time = 0) {
  auto sc = StrChan::create();
  start([sc, label, min_time]() {
    while (true) {
      if (!(sc << label))
        break;
      Time::sleep(Time::MilliSecond * (min_time + (rand() % 1000)));
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
      msg << b1;
      dbg("%s\n", msg);
      msg << b2;
      dbg("%s\n", msg);
    }
    dbg("Bye\n");
  });

  // Only wait for co2
  while (isHandle(co2))
    executeActives();
  close(b1);
  close(b2);
  dbg("Done\n");
}

// -------------------------------------------------
THandle readChannel(StrChan c, int n) {
  return start([c, n]() {
    for (int i = 0; i < n; ++i) {
      const char* msg = nullptr;
      if (!pull(c, msg))
        break;
      dbg("%s\n", msg);
    }
    dbg("Bye\n");
  });
}


StrChan fanIn(StrChan a, StrChan b) {
  auto c = StrChan::create();

  start([c, a]() {
    const char* msg;
    while (msg << a)
      c << msg;
  });

  start([c, b]() {
    while (true) {
      const char* msg;
      if (!pull(b, msg))
        break;
      push(c, msg);
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

  close(b);
  close(b1);
  close(b2);

  dbg("Done\n");
}

// -------------------------------------------
StrChan fanInSelect(StrChan a, StrChan b) {
  auto c = StrChan::create();

  start([c, a, b]() {
    while (true) {

      // Wait until we can 'read' from any of those channels
      const char* msg;
      TWatchedEvent we[3] = { 
        TWatchedEvent(a.asU32(), eEventType::EVT_CHANNEL_CAN_PULL ), 
        TWatchedEvent(b.asU32(), eEventType::EVT_CHANNEL_CAN_PULL ),
        Time::after( 400 )
      };
      int n = wait(we, 3);
      if (n == 2 || n == -1) {
        dbg("Too slows..\n");
        close(c);
        break;
      }
      StrChan ic = (n == 0) ? a : b;

      if (!( msg << ic))
        break;

      c << msg;
    }
  });

  return c;
}

// --------------------------------------------------------------
// User code
StrChan fanInSelect2(StrChan a, StrChan b) {
  auto c = StrChan::create();

  start([c, a, b]() {
    while (true) {
      
      // Wait until we can 'read' from any of those channels
      int n = choose(
        ifCanPull(a, [c](const char* msg) {
          dbg("Hi, I'm A and pulled data %s\n", msg);
          c << msg;
        }),
        ifCanPull(b, [c](auto msg) {
          dbg("Hi, I'm B and pulled data %s\n", msg);
          c << msg;
        }),
        ifTimeout(400, []() {
          dbg("Timeout waiting for a or b\n");
        })
      );

      if (n == 2 || n == -1) {
        dbg("Too slows..\n");
        close(c);
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

  close(b);
  close(b1);
  close(b2);
}

// ----------------------------------------------------------
void sample_go() {
//  test_concurrency();
//  test_borings();
//  test_fanIn();
  test_select();
}

