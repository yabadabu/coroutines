#include <cstdarg>
#include <cstdio>
#include <vector>
#include "sample.h"

using namespace Coroutines;
using Coroutines::wait;

// -------------------------------------------------------------
typedef TTypedChannel<int> IntChan;

StrChan boring(const char* label, TTimeDelta min_time);
THandle readChannel(StrChan c, int max_reads);

// ---------------------------------------------------------
void test_every_and_after() {
  TSimpleDemo demo("test_every_and_after");

  auto t1 = every( 2 * Time::Second);
  auto t2 = after( 5 * Time::Second);

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

// ---------------------------------------------------------
void test_new_choose() {
  TSimpleDemo demo("test_new_choose");

  auto c1 = boring("John", Time::Second);
  auto c2 = boring("Peter", Time::Second);
  auto f = StrChan::create();

  auto coA = start([c1,c2,f]() {
    while (true) {
      int n = choose(
        ifCanPull(c1, [f](const char* msg) {
          f << msg;
        }),
        ifCanPull(c2, [f](const char* msg) {
          f << msg;
        }),
        ifTimeout(1500 * Time::MilliSecond, []() {
          dbg("Timeout waiting for a or b\n");
        })
      );
      if (n == 2) {
        dbg("Timeout triggered in choose condition\n");
        break;
      }
      else if (n == -1) {
        // Happens when c1 is closed from another co...
        dbg("Exit the choose condition with an error\n");
        break;
      }
    }
    close(f);
  });

  auto co2 = readChannel(f, 5);
  start([&]() {
    // wait co2 means wait until readChannel reads 5 entries
    // or his channel is closed
    wait(co2);
    // Closing boring producerchannels 
    close(c1);
    close(c2);
    // Ensure the fanin is closed
    close(f);
  });
}

// --------------------------------------------------
// go will push, then recv, push, recv, push recv, close, done
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
// Confirm we can read from a closed channel while 
// there is still data inside
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
    auto start_ts = Time::now();
    auto ticker = every(500 * Time::MilliSecond);
    start([&]() {
      TTimeStamp ts;
      while (ts << ticker) {
        dbg("Tick at %s\n", Time::asStr(ts - start_ts).c_str());
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

// ---------------------------------
void test_chain_of_channels() {
  TSimpleDemo demo("test_chain_of_channels");
  start([]() {
 
    int n = 1000;
    
    // Create N channels
    std::vector< IntChan > channels;
    for (int i = 0; i < n; ++i)
      channels.push_back(IntChan::create());

    // Create N-1 coroutines
    for (int i = 0; i < n-1; ++i) {
      auto cprev = channels[i];
      auto cnext = channels[i + 1];
      auto co = start([cprev, cnext]() {
        while (true) {
          int id;
          // If there is nothing to read, exit
          if (!(id << cprev))
            break;
          ++id;
          cnext << id;
        }
        close(cnext);
      });
    }
    dbg("All setup done. Pushing 1 to the first channel\n");

    // Send a signal
    channels[0] << 1;
    // Read back the result
    int result;
    result << channels.back();
    dbg("We sent 1, and received %d\n", result);
    assert(result == n);
    // Close all channels in cascade
    close(channels[0]);
  });
}

// ---------------------------------
void test_non_pods() {
  TSimpleDemo demo("test_non_pods");
  start([]() {
    typedef TTypedChannel< std::string > StringChan;
    auto c1 = StringChan::create(5);
    c1 << std::string("john");
    c1 << std::string("Peter" );

    auto g2 = start([c1]() {
      std::string s;
      while (s << c1) {
        dbg("Recv %s\n", s.c_str());
      }
    });

    close(c1);
    wait(g2);
  });
}


void sample_new_channels() {
  test_every_and_after();
  //test_go_closing_channels();
  //test_read_closed_channels();
  //test_tickers();
  //test_go_worker_pool();
  //test_new_choose();
  //test_chain_of_channels();
  //test_non_pods();
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
