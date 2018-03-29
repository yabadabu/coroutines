#include <cstdarg>
#include <cstdio>
#include <vector>
#include "sample.h"

using namespace Coroutines;
using Coroutines::wait;

// -----------------------------------------------------------
void test_concurrency() {

  auto sc = StrChan::create();

  // Writes a label and waits 1 sec
  auto co1 = start(std::bind([sc]( const char* label ){
    while (true) {
      sc << label;
      Time::sleep(Time::Second);
    }
  }, "john"));

  // Reads from channel 5 times and then exists
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
// Create a coroutine which emits label at random intervals of 1 sec
// returns the channel created
StrChan boring(const char* label, TTimeDelta min_time = TTimeDelta::zero()) {
  auto sc = StrChan::create();
  start([sc, label, min_time]() {
    while (true) {
      if (!(sc << label))
        break;
      Time::sleep(min_time + Time::MilliSecond * ((rand() % 1000)));
    }
    dbg("Boring %s exits\n", label);
  });
  return sc;
}

// Testing sequenciacion
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
// Outputs to console the first N lines read from the given channel
THandle readChannel(StrChan c, int n) {
  return start([c, n]() {
    for (int i = 0; i < n; ++i) {
      const char* msg = nullptr;
      if (!(c << msg))
        break;
      dbg("%s\n", msg);
    }
    dbg("read Channels ends\n");
  });
}

// -------------------------------------------------
// Merges contents of channels a & b into a single channel c
// -------------------------------------------------
StrChan fanIn(StrChan a, StrChan b) {
  auto c = StrChan::create();

  // Reads from a and writes to c, using <<
  start([c, a]() {
    const char* msg;
    while (msg << a)
      c << msg;
  });

  // Reads from b and writes to c, using pull/push
  start([c, b]() {
    while (true) {
      const char* msg;
      if (!(msg << b))
        break;
      c << msg;
    }
  });
  return c;
}

// -------------------------------------------------
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
// Fan In implemented using wait
StrChan fanInWithWait(StrChan a, StrChan b) {
  auto c = StrChan::create();

  start([c, a, b]() {
    while (true) {

      // Wait until we can 'read' from any of those channels
      // or 400ms without activity are triggered
      const char* msg;
      TWatchedEvent we[3] = { 
        TWatchedEvent(a, eEventType::EVT_CHANNEL_CAN_PULL ), 
        TWatchedEvent(b, eEventType::EVT_CHANNEL_CAN_PULL ),
        TWatchedEvent( 400 * Time::MilliSecond )
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

void test_select_with_wait() {
  TSimpleDemo demo("test_select_with_wait");

  auto b1 = boring("john", 0 * Time::Second);
  auto b2 = boring("peter", 0 * Time::Second);

  auto b = fanInWithWait(b1, b2);
  auto co2 = readChannel(b, 10);

  while (isHandle(co2))
    executeActives();

  close(b);
  close(b1);
  close(b2);
}

// --------------------------------------------------------------
// User code
StrChan fanInChoose(StrChan a, StrChan b) {
  auto c = StrChan::create();

  start([c, a, b]() {
    while (true) {
      
      // Wait until we can 'read' from any of those channels
      int n = choose(
        ifCanPull(a, [c](const char* msg) {
          c << msg;
        }),
        ifCanPull(b, [c](const char* msg) {
          c << msg;
        }),
        ifTimeout(400 * Time::MilliSecond, []() {
          dbg("Timeout waiting for a or b\n");
        })
      );

      if (n == 2 || n == -1) {
        dbg("Too slows... (%d)\n", n);
        close(c);
        break;
      }
    }

  });

  return c;
}

void test_choose() {
  TSimpleDemo demo("test_choose");

  auto b1 = boring("john");
  auto b2 = boring("peter");
  auto b = fanInChoose(b1, b2);
  auto co2 = readChannel(b, 10);

  while (isHandle(co2))
    executeActives();

  close(b);
  close(b1);
  close(b2);
}

// ----------------------------------------------------------
void sample_go() {
  test_concurrency();
  test_borings();
  test_fanIn();
  test_select_with_wait();
  test_choose();
}

