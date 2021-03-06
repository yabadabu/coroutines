#include <cstdarg>
#include <cstdio>
#include <vector>
#include "sample.h"

using namespace Coroutines;
using Coroutines::wait;

// -----------------------------------------------------------
void demo_yield(const char* title) {
  dbg("%s boots\n", title);
  yield();
  dbg("%s after yield\n", title);
  yield();
  dbg("%s after yield 2\n", title);
  yield();
  dbg("%s leaves\n", title);
}

void test_yield() {
  TSimpleDemo demo("test_demo_yield");
  auto f1 = []() { demo_yield("co1"); };
  auto f2 = []() { demo_yield("co2"); };
  auto co1 = start(f1);
  auto co2 = start(f2);
  auto co3 = start([]() {
    dbg("At co3. Enter and exit\n");
  });
}

// -----------------------------------------------------------
void basic_wait_time(const char* title, TTimeDelta amount_of_time) {
  dbg("%s boots. Will wait %d msecs\n", title, Time::asMilliSeconds( amount_of_time ));
  wait(amount_of_time);
  dbg("%s After waiting %d msecs we leave\n", title, Time::asMilliSeconds(amount_of_time));
}

void test_wait_time() {
  TScopedTime tm;
  {
    TSimpleDemo demo("test_wait_time");
    auto co1 = start([]() { basic_wait_time("co1", 3 * Time::Second); });
    auto co2 = start([]() { basic_wait_time("co2", 5 * Time::Second); });
  }
  auto elapsed = tm.elapsed();
  dbg("test_wait_time expected to finish in %ld msecs, and finished in %ld...\n", Time::asMilliSeconds( 5 * Time::Second ), Time::asMilliSeconds( elapsed ) );
  assert( abs( Time::asMilliSeconds( elapsed - ( 5 * Time::Second ) ) ) < 10 );
}

// -----------------------------------------------------------
void test_wait_all() {
  TScopedTime tm;
  {
    TSimpleDemo demo("test_wait_all");
    auto co1 = start([]() {
      auto coA = start([]() {basic_wait_time("A", 2500 * Time::MilliSecond); });
      auto coB = start([]() {basic_wait_time("B", 1000 * Time::MilliSecond); });
      auto coC = start([]() {basic_wait_time("C", 1500 * Time::MilliSecond); });

      // Waits for all co end before continuing...
      waitAll( coA, coB, coC );
      dbg("waitAll continues...\n");
    });
  }
  TTimeDelta elapsed = tm.elapsed();
  dbg("waitAll expected to finish in %d msecs, and finished in %d...\n", 2500, Time::asMilliSeconds( elapsed ));
  assert( abs( Time::asMilliSeconds( elapsed - 2500 * Time::MilliSecond) ) < 12 );
}

// ---------------------------------------------------------
// Wait while the key is not pressed 
void waitKey(int c) {
  wait([c]() { return !isKeyPressed(c); });
}

void test_wait_keys() {
  TSimpleDemo demo("test_wait_keys");
  auto coKeys = start([]() {
    dbg("At coKeys. Press the key 'A'\n");
    waitKey('A');
    dbg("At coKeys. Now press the key 'B'\n");
    waitKey('B');
    dbg("At coKeys. well done\n");
  }); 
}

// ---------------------------------------------------------
// Wait for any of the two coroutines to finish or timeout
void test_wait_2_coroutines_with_timeout() {
  TSimpleDemo demo("test_wait_2_coroutines_with_timeout");

  auto coA = start([]() {basic_wait_time("A", 13 * Time::Second); });
  auto coB = start([]() {basic_wait_time("B", 8 * Time::Second); });
  dbg("co to wait are %08x %08x (%p %p)\n", coA.asUnsigned(), coB.asUnsigned(), &coA, &coB);

  auto co2 = start([coA, coB]() {
    // Get a copy or the input values will be corrupted when co2 goes out of scope
    THandle tcoA = coA;
    THandle tcoB = coB;
    int niter = 0;
    while (true) {
      dbg("co2 iter %d %08x %08x (%p %p)\n", niter, coA.asUnsigned(), coB.asUnsigned(), &coA, &coB);
      ++niter;
      int n = 0;
      TWatchedEvent evts[3];
      if (isHandle(tcoA))
        evts[n++] = tcoA;
      if (isHandle(tcoB))
        evts[n++] = tcoB;
      if (!n) {
        dbg("Nothing else to wait\n");
        break;
      }
      dbg("co2 goes to sleep for 5s waiting for coA and/or coB to end (%d)\n", n);
      evts[n++] = 5 * Time::Second;
      int k = wait(evts, n);
      if (k == (n-1))
        dbg("co2 timedout\n");
      else
        dbg("co2 resumes for event %d\n", k);
    }
    dbg("co2 ends\n");
  });
}

// ---------------------------------------------------------
// Wait for any of the two coroutines to finish or timeout
void test_user_events() {
  TSimpleDemo demo("test_user_events");

  TEventID evt1 = createEvent();;
  TEventID evt2 = createEvent();;

  // Waits 1s, sets Evt2, waits 1s, sets Evt1

  auto coA = start([evt1,evt2]() {
    basic_wait_time("A2", 1000 * Time::MilliSecond );
    dbg("A. Setting evt2\n");
    setEvent(evt2);
    basic_wait_time("A1", 1000 * Time::MilliSecond );
    dbg("A. Setting evt1\n");
    setEvent(evt1);
  });

  auto coB1 = start([evt1,evt2]() {
    
    TWatchedEvent we[2];
    while (true) {
      int n = 0;
      // Register only to the active events
      if (!isEventSet(evt1))
        we[n++] = evt1;
      if (!isEventSet(evt2))
        we[n++] = evt2;
      if (!n)
        break;
      dbg("B1. Waiting for %d events\n", n);
      int idx = wait(we, n);
      dbg("B1. Event idx %d/%d triggered!\n", idx, n);
    }

    dbg("B1. Done\n");
  });

  auto coB2 = start([evt1, evt2]() {
    dbg("B2. I'm waiting for the two events using waitAll\n");
    waitAll(evt1, evt2);
    dbg("B2. Done\n");
  });

  auto coB3 = start([coA, evt1]() {
    dbg("B3. I'm waiting a mixing of coA and evt1\n");
    waitAll(coA, evt1);
    dbg("B3. Done\n");
  });

  auto coC = start([coA, coB1, coB2, coB3, evt1, evt2]() {
    dbg("C. Waiting for all co's to finish\n");
    waitAll( coA, coB1, coB2, coB3 );
    // Clear the events
    destroyEvent(evt1);
    destroyEvent(evt2);
    assert(!isValidEvent(evt1));
    assert(!isValidEvent(evt2));
    dbg("C. All cleared\n");
  });

}

// ----------------------------------------------------------
void sample_wait() {
  test_user_events();
  test_yield();
  test_wait_time();
  test_wait_all();
  test_wait_keys();
  test_wait_2_coroutines_with_timeout();
}
