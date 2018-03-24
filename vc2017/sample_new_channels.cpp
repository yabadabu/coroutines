#include <cstdarg>
#include <cstdio>
#include <vector>
#include "sample.h"
#include "coroutines/choose.h"

using namespace Coroutines;

// -------------------------------------------------------------
// -------------------------------------------------------------
// -------------------------------------------------------------
typedef TTypedChannel<const char*> StrChan;
typedef TTypedChannel<int> IntChan;

// ---------------------------------------------------------
TTypedChannel<const char*> new_boring(const char* label, TTimeDelta min_time = 0) {
  auto sc = StrChan::create();
  start([sc, label, min_time]() {
    while (true) {
      dbg("Try to push %sto c:%08x\n", label, sc);
      if (!( sc << label))
        break;
      dbg("Pushed %s, no waiting a bit\n", label);
      Time::sleep(min_time + (rand() % 1000) * Time::MilliSecond);
    }
  });
  return sc;
}

// -------------------------------------------------
THandle new_readChannel(TTypedChannel<const char*> c, int max_reads) {
  return start([c, max_reads]() {
    for (int i = 0; i < max_reads; ++i) {
      const char* msg = nullptr;
      if (!pull(c, msg))
        break;
      dbg("Read [%d] %s\n", i, msg);
    }
    dbg("Bye\n");
  });
}


// ---------------------------------------------------------
void test_every_and_after() {
  TSimpleDemo demo("test_every_and_after");

  auto t1 = every( 3 * Time::Second);
  auto t2 = after( 4 * Time::Second);

  auto coT2 = start([t1, t2]() {
    pull(t2);
    dbg("After t2 timeouts..., closing the 'every' channel t1\n");
    close(t1);
  });

  auto coT = start([t1]() {
    while (pull(t1))
      dbg(".\n");
    dbg("End of events\n");
  });

}


void test_new_choose() {
  TSimpleDemo demo("test_new_choose");

  auto c1 = new_boring("John", Time::Second);
  auto c2 = new_boring("Peter", Time::Second);
  auto o1 = StrChan::create();

  auto coA = start([c1,c2,o1]() {
    while (true) {
      int n = choose(
        ifCanPull(c1, [c1, o1](const char* msg) {
          dbg("Hi, I'm A and pulled data %s\n", msg);
          push(o1, msg);
        }),
        ifCanPull(c2, [c2, o1](auto msg) {
          dbg("Hi, I'm B and pulled data %s\n", msg);
          push(o1, msg);
        })
        //  ,
        //ifTimeout(400, []() {
        //  dbg("Timeout waiting for a or b\n");
        //})
        );
      dbg("Choose returned %d\n", n);
    }
  });

  auto co2 = new_readChannel(o1, 5);
  start([&]() {
    wait(co2);
    close(c1);
    close(c2);
    close(o1);
  });
}

// --------------------------------------------------
// go will push, the recv, push, recv, push recv, close, done
// here will: push push push, close, recv, recv, recv, done
void test_go_closing_channels() {
  TSimpleDemo demo("test_go_closing_channels");
  auto jobs = TTypedChannel<int>::create(5);
  auto done = TTypedChannel<bool>::create();
  start([&]() {
    while (true) {
      int j;
      if (j << jobs) {
        dbg("Received job %d\n", j);
      }
      else {
        dbg("Received all jobs!\n");
        done << true;
        break;
      }
    }
  });

  start([&]() {
    for (int i = 1; i <= 3; ++i) {
      jobs << i;
      dbg("Sent job %d\n", i);
    }
    close(jobs);
    dbg("sent all jobs, channel is now closed\n");

    pull(done);
    dbg("Jobs work finished\n");
  });

}


// --------------------------------------------------
void test_read_closed_channels() {
  TSimpleDemo demo("test_read_closed_channels");
  start([]() {
    auto queue = StrChan::create(2);
    queue << "one";
    queue << "two";
    close(queue);
    const char* msg;
    while (msg << queue) {
      dbg("Recv %s\n", msg);
    }
  });
}

// --------------------------------------------------
void test_tickers() {
  TSimpleDemo demo("test_tickers");
  start([]() {
    auto ticker = every(500 * Time::MilliSecond);
    start([ticker]() {
      TTimeStamp ts;
      while (ts << ticker) {
        long num_secs, num_millisecs;
        getSecondsAndMilliseconds(ts, &num_secs, &num_millisecs);
        num_secs = num_secs % 60;
        long num_mins = num_secs / 60;
        dbg("Tick at %ld:%ld:%03ld\n", num_mins, num_secs, num_millisecs);
      }
    });
    Time::sleep(1600 * Time::MilliSecond);
    close(ticker);
    dbg("Ticker stopped\n");
  });
}

// --------------------------------------------------
void go_worker(int id, TTypedChannel<int> jobs, TTypedChannel<int> results) {
  int j;
  while (j << jobs) {
    dbg("Worker %d started job %d\n", id, j);
    Time::sleep(Time::Second);
    dbg("Worker %d finished job %d\n", id, j);
    results << (j * 2);
  }
}

void test_go_worker_pool() {
  TSimpleDemo demo("test_go_worker_pool");
  start([]() {
    auto jobs = IntChan::create(100);
    auto results = IntChan::create(100);
    for (int i = 1; i <= 3; ++i) {
      start([&,i]() {   // We need by value
        go_worker(i, jobs, results);
      });
    }
    dbg("Sending work...\n");
    for (int i = 1; i <= 5; ++i)
      jobs << i;
    close(jobs);
    dbg("Receiving results...\n");
    for (int i = 0; i < 5; ++i) {
      int r;
      r << results;
      dbg("Result %d is %d\n", i, r);
    }
    dbg("Done\n");
  });
}

void sample_new_channels() {
  //test_go_closing_channels();
  //test_every_and_after();
  //test_read_closed_channels();
  //test_tickers();
  test_go_worker_pool();
  //test_new_choose();
}


// Can't push (will block) unless there is someone than is pulling
// unless the channel is buffered



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
