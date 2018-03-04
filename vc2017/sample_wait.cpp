#define _CRT_SECURE_NO_WARNINGS
#include <cstdarg>
#include <cstdio>
#include <vector>
#include "sample.h"

using namespace Coroutines;

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
void basic_wait_time(const char* title, int milli_secs) {
  dbg("%s boots. Will wait %d milli_secs\n", title, milli_secs);
  wait(nullptr, 0, milli_secs);
  dbg("%s After waiting %d ticks we leave\n", title, milli_secs);
}

void test_wait_time() {
  TScopedTime tm;
  {
    TSimpleDemo demo("test_wait_time");
    auto co1 = start([]() { basic_wait_time("co1", 3000); });
    auto co2 = start([]() { basic_wait_time("co2", 5000); });
  }
  auto elapsed = tm.elapsed();
  assert(elapsed == 5000);
}

// -----------------------------------------------------------
void test_wait_all() {
  TScopedTime tm;
  {
    TSimpleDemo demo("test_wait_all");
    auto co1 = start([]() {
      auto coA = start([]() {basic_wait_time("A", 2500); });
      auto coB = start([]() {basic_wait_time("B", 1000); });
      auto coC = start([]() {basic_wait_time("C", 1500); });

      // Waits for all co end before continuing...
      waitAll({ coA, coB, coC });
      dbg("waitAll continues...\n");
    });
  }
  TTimeStamp elapsed = tm.elapsed();
  dbg("waitAll expected to finish in %d msecs, and finished in %d...\n", 2500, elapsed );
  assert( abs( (int)elapsed - 2500 ) < 10 );
}

// ---------------------------------------------------------
// Wait while the key is not pressed 
void waitKey(int c) {
  wait([c]() { return (::GetAsyncKeyState(c) & 0x8000) == 0; });
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

  auto coA = start([]() {basic_wait_time("A", 13); });
  auto coB = start([]() {basic_wait_time("B", 8); });
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
      TWatchedEvent evts[2];
      if (isHandle(tcoA))
        evts[n++] = tcoA;
      if (isHandle(tcoB))
        evts[n++] = tcoB;
      if (!n) {
        dbg("Nothing else to wait\n");
        break;
      }
      dbg("co2 goes to sleep for 5s waiting for coA and/or coB to end (%d)\n", n);
      int k = wait(evts, n, 5);
      if (k == wait_timedout)
        dbg("co2 timedout\n");
      else
        dbg("co2 resumes for event %d\n", k);
    }
    dbg("co2 ends\n");
  });
}

// ----------------------------------------------------------
void sample_wait() {
  test_yield();
  test_wait_time();
  test_wait_all();
  test_wait_keys();
  test_wait_2_coroutines_with_timeout();
}
