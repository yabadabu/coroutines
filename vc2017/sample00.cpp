#define _CRT_SECURE_NO_WARNINGS
#include "coroutines/coroutines.h"
#include <cstdarg>
#include <cstdio>
#include "coroutines/io_channel.h"

using namespace Coroutines;

// --------------------------------------------------
void dbg(const char *fmt, ...) {
  char buf[1024];
  va_list ap;
  va_start(ap, fmt);
  int n = _vsnprintf_s(buf, sizeof(buf) - 1, fmt, ap);
  if (n < 0)
    buf[1023] = 0x00;
  va_end(ap);
  printf("%04d:%02d.%02d %s", (int)now(), current().id, current().age, buf);
}


void runUntilAllCoroutinesEnd() {
  while (true) {
    updateCurrentTime(1);
    if (!executeActives())
      break;
  }
  dbg("all done\n");
}

struct TSimpleDemo {
  const char* title;
  TSimpleDemo(const char* new_title) : title( new_title ) {
    dbg("-------------------------------\n%s starts\n", title);
  }
  ~TSimpleDemo() {
    dbg("%s waiting co's to finish\n", title);
    runUntilAllCoroutinesEnd();
    dbg("%s ends\n", title);
  }
};

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

void test_demo_yield() {
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
void basic_wait_time(const char* title, int nsecs) {
  dbg("%s boots. Will wait %d secs\n", title, nsecs);
  wait(nullptr, 0, nsecs);
  dbg("%s After waiting %d ticks we leave\n", title, nsecs);
}

void test_wait_time() {
  TScopedTime tm;
  {
    TSimpleDemo demo("test_wait_time");
    auto co1 = start([]() { basic_wait_time("co1", 3); });
    auto co2 = start([]() { basic_wait_time("co2", 5); });
  }
  auto elapsed = tm.elapsed();
  assert(elapsed == 5);
}

// -----------------------------------------------------------
void test_wait_co() {
  TScopedTime tm;
  {
    TSimpleDemo demo("test_wait_co");
    auto co1 = start([]() { basic_wait_time("co1", 3); });
    start([co1]() {
      dbg("Co2: Waiting for co1\n");
      wait(co1);
      dbg("Co2: co1 is ready. continuing\n");
    });
  }
  assert(tm.elapsed() == 3);
}
  


// -----------------------------------------------------------
void test_wait_all() {
  TScopedTime tm;
  {
    TSimpleDemo demo("test_wait_all");
    auto co1 = start([]() {
      auto coA = start([]() {basic_wait_time("A", 25); });
      auto coB = start([]() {basic_wait_time("B", 10); });
      auto coC = start([]() {basic_wait_time("C", 15); });

      // Waits for all co end before continuing...
      waitAll({ coA, coB, coC });
      dbg("waitAll continues...\n");
    });
  }
  assert(tm.elapsed() == 26);
}

// ---------------------------------------------------------
// Wait while the key is not pressed 
void waitKey(int c) {
  wait( [c]() { return (::GetAsyncKeyState(c) & 0x8000) == 0; });
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
      if( k == wait_timedout)
        dbg("co2 timedout\n");
      else
        dbg("co2 resumes for event %d\n", k);
    }
    dbg("co2 ends\n");
  });
  //runUntilAllCoroutinesEnd();
}

// ---------------------------------------------------------
// Wait for any of the two coroutines to finish or timeout
void test_channels() {
  resetTimer();
  TSimpleDemo demo("test_channels");

  // to send/recv data between co's
  TChannel* ch1 = new TChannel(3, sizeof(int));
  dbg("ch is %p\n", ch1);

  // co1 consumes
  auto co1 = start([ch1]() {
    dbg("co1 begin\n");

    while (true) {
      int data = 0;
      // if there is nothing it will block us until someone pushes something
      // or the channel is closed.
      if (!pull(ch1, data))
        break;
      dbg("co1 has pulled %d\n", data);
    }

    dbg("co1 end\n");
    assert(now() == 2);
  });

  // co2 produces 10 elems
  auto co2 = start([ch1]() {
    dbg("co2 begin\n");

    // We can only fit 3 elems in the channel. When trying to push the 4th it will block us
    // yielding this co
    for (int i = 0; i < 5; ++i) {
      int v = 100 + i;
      push(ch1, v);
      dbg("co2 has pushed %d\n", v);
    }

    // If I close, pulling from ch1 will return false once all elems have been pulled
    ch1->close();

    dbg("co2 ends\n");
    assert(now() == 1 || now() == 2);
  });

  //for( int i=0; i<3; ++i )
  //  push(ch1, i);
  //dbg("Closing ch1\n");
  //ch1->close();
  runUntilAllCoroutinesEnd();
}

// ----------------------------------------
void test_channels_send_from_main() {
  TSimpleDemo demo("test_channels");

  // send data between co's
  TChannel* ch1 = new TChannel(5, sizeof(int));
  dbg("ch is %p\n", ch1);
  assert(ch1->bytesPerElem() == 4);

  // co1 consumes
  auto co1 = start([ch1]() {
    dbg("co1 begin\n");
    assert(ch1->bytesPerElem() == 4);
    while (true) {
      int data = 0;
      if (!pull(ch1, data))
        break;
      dbg("co1 has pulled %d from %p\n", data, ch1);
    }

    dbg("co1 end\n");
  });

  int v = 100;
  dbg("Main pushes 100 twice and then closes\n");
  push(ch1, v);
  push(ch1, v);
  ch1->close();
  runUntilAllCoroutinesEnd();
}

// -----------------------------------------------------------
#include <vector>
struct TBuffer : public std::vector< uint8_t > {
  TBuffer(size_t initial_size) {
    resize(initial_size);
  }
};

// ---------------------------------------------------------------
// -----------------------------------------------------------
extern void test_app2();
extern void sample_net();
extern void sample_create();

// -----------------------------------------------------------
int main(int argc, char** argv) {
  if (0) {
    test_demo_yield();
    test_wait_time();
    test_wait_co();
    test_wait_all();
    //test_wait_keys();
    test_wait_2_coroutines_with_timeout();
    test_channels();
    test_channels_send_from_main();
  }
  else 
  {
    //test_app2();
    //sample_create();
    sample_net();
  }
  return 0;
}


/*
    



*/
