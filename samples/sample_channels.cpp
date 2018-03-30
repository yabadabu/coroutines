#include <cstdarg>
#include <cstdio>
#include <vector>
#include "sample.h"

using namespace Coroutines;
using Coroutines::wait;

typedef TTypedChannel<int> IntsChannel;

// ---------------------------------------------------------
// Wait for any of the two coroutines to finish or timeout
void test_channels() {
  TSimpleDemo demo("test_channels");

  // to send/recv data between co's
  auto ch1 = IntsChannel::create(3);
  dbg("ch is %p\n", ch1);

  // co1 consumes
  auto co1 = start([ch1]() {
    dbg("co1 begin\n");

    while (true) {
      int data = 0;
      // if there is nothing it will block us until someone pushes something
      // or the channel is closed.
      if (!(data << ch1))
        break;
      dbg("co1 has pulled %d\n", data);
    }

    dbg("co1 end\n");
  });

  // co2 produces 10 elems
  auto co2 = start([ch1]() {
    dbg("co2 begin\n");

    // We can only fit 3 elems in the channel. When trying to push the 4th it will block us
    // yielding this co
    for (int i = 0; i < 5; ++i) {
      int v = 100 + i;
      ch1 << v;
      dbg("co2 has pushed %d\n", v);
    }

    // If I close, pulling from ch1 will return false once all elems have been pulled
    close(ch1);

    dbg("co2 ends\n");
  });

  //for( int i=0; i<3; ++i )
  //  push(ch1, i);
  //dbg("Closing ch1\n");
  //ch1->close();
}

// ----------------------------------------
void test_channels_send_from_main() {
  TSimpleDemo demo("test_channels");

  // send data between co's
  auto ch1 = TTypedChannel<int>::create(5);
  dbg("ch is %p\n", ch1);

  // co1 consumes
  auto co1 = start([ch1]() {
    dbg("co1 begin\n");
    while (true) {
      int data = 0;
      if (!(data << ch1))
        break;
      dbg("co1 has pulled %d from %p\n", data, ch1);
    }

    dbg("co1 end\n");
  });

  int v = 100;
  dbg("Main pushes 100 twice and then closes\n");
  ch1 << v;
  ch1 << v;
  close(ch1);
}

void test_consumers() {
  TSimpleDemo demo("test_consumers");

  // Because we can't wait from the main thread
  start([]() {
    // Create a channel
    auto ch1 = TTypedChannel<int>::create(32);
  
    // 3 consumers
    std::vector<THandle> consumers;
    for (int i = 0; i<3; ++i) {
      consumers.push_back( start([i, ch1]() {
        int id;
        while (id << ch1) {
          dbg("[%d] Consumed %d\n", i, id);
        };
        dbg("[%d] Leaves\n", i);
      }));
    }

    auto producer = start([ch1]() {
      int ids[] = { 2,3,5,7,11,13 };
      for (auto id : ids) {
        dbg("Producing %d\n", id);
        ch1 << id;
        wait(Time::Second);
      }
      close(ch1);
    });

    waitAll(consumers, producer);
    dbg("All done\n");
  });

}

// ----------------------------------------------------------
void sample_channels() {
  test_consumers();
  //test_channels();
  //test_channels_send_from_main();
}
