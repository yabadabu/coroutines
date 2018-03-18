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

template< typename T >
bool operator<=(T &p, TChannel<T>& c) {
  return c.pull(p);
}

template< typename T >
bool operator<=(TChannel<T>& c, T &p) {
  return c.push(p);
}


void test_concurrency() {

  auto sc = new StrChan();

  auto co1 = start(std::bind([sc]( const char* label ){
    while (true) {
      sc->push(label);
      //*sc <= label;
      Time::sleep(Time::milliseconds(1000));
    }
  }, "john"));

  auto co2 = start([sc]() {
    for (int i = 0; i < 5; ++i) {
      const char* msg;
      if( msg <= *sc )
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

// -------------------------------------------
template< typename T>
struct ifCanPullDef {
  TWatchedEvent                we;
  TChannel<T>*                 channel;
  T                            obj;
  std::function<void(T& obj)>  cb;
  ifCanPullDef(TChannel<T>* new_channel, std::function< void(T) > new_cb)
    : we(new_channel, obj, eEventType::EVT_CHANNEL_CAN_PULL)
    , channel(new_channel)
    , cb( new_cb )
  { }
  void run() {
    channel->pull(obj);
    cb(obj);
  }
};

template< typename T, typename TFn >
ifCanPullDef<T> ifCanPull(TChannel<T>* chan, TFn new_cb) {
  return ifCanPullDef<T>(chan, new_cb);
}


struct ifTimeout {
  TWatchedEvent                we;
  std::function<void(void)>    cb;
  ifTimeout(TTimeDelta delta)
    : we(Time::after(delta))
  { }
  void run() {
    cb();
  }
};

// --------------------------------------------
void fillChoose(TWatchedEvent* we) { }

template< typename A, typename ...Args >
void fillChoose(TWatchedEvent* we, A& a, Args... args) {
  *we = a.we;
  fillChoose(we + 1, args...);
}

// --------------------------------------------
void runOption(int idx, int the_option) {
  assert("Something weird is going on...\n");
}

template< typename A, typename ...Args >
void runOption(int idx, int the_option, A& a, Args... args) {
  if (idx == the_option)
    a.run();
  else
    runOption(idx + 1, the_option, args...);
}

template< typename A, typename ...Args >
int choose( A& a, Args... args ) {
  auto nwe = 1 + (int) sizeof...(args);
  TWatchedEvent wes[1 + sizeof...(args)];
  fillChoose(&wes[0], a, args...);
  int n = wait(wes, nwe);
  if (n >= 0 && n < nwe)
    runOption(0, n, a, args...);
  return n;
}


StrChan* fanInSelect2(StrChan* a, StrChan* b) {
  StrChan* c = new StrChan;

  start([c, a, b]() {
    while (true) {
      
      // Wait until we can 'read' from any of those channels
      int n = choose(
        ifCanPull(a, [c](auto msg)->void {
          dbg("Hi, I'm A and pulled data %s\n", msg);
          c->push(msg);
        }),
        ifCanPull(b, [c](auto msg)->void {
          dbg("Hi, I'm B and pulled data %s\n", msg);
          c->push(msg);
        }),
        ifTimeout( 400 )
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
