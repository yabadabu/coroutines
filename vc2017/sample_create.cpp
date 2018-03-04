#define _CRT_SECURE_NO_WARNINGS
#include <cstdarg>
#include <cstdio>
#include <vector>
#include "sample.h"

using namespace Coroutines;

extern void dbg(const char *fmt, ...);
extern void waitKeyPress(int c);
extern void runUntilAllCoroutinesEnd();

void waitKeyPress(int c) {
  wait([c]() { 
    return (::GetAsyncKeyState(c) & 0x0001) == 0; 
  });
}

// ---------------------------------------------------------
static void doSecond(int max_n) {
  int key = 'W';
  int n = 0;
  dbg("doSecond: started. Press %c %d times\n", key, max_n);
  while (n < max_n) {
    ++n;
    dbg("doSecond: waiting for key %c (%d/%d)\n", key, n, max_n);
    waitKeyPress(key);
    dbg("doSecond: key %c has been pressed\n", key);
  }

  dbg("doSecond: ends\n");
}

// ---------------------------------------------------------
static void doSpawner() {

  printf("&doSpawner = %p\n", &doSpawner);
  printf("&doSecond  = %p\n", &doSecond);

  int key = 'T';
  int n = 0;
  int max_n = 3;
  dbg("doSpawner: started. Press %c %d times\n", key, max_n);
  while ( n < max_n ) {
    ++n;

    dbg("doSpawner: waiting for key %c (%d/%d)\n", key, n, max_n);
    waitKeyPress(key);
    dbg("doSpawner: key %c has been pressed\n", key);

    start([]() { doSecond(3); });

  }

  dbg("doSpawner: ends\n");
}

// ----------------------------------------------------------
void test_create_from_co() {
  TSimpleDemo demo("test_create_from_co");
  start( &doSpawner);
}

// ----------------------------------------------------------
void test_self_destroy() {
  TSimpleDemo demo("test_self_destroy");
  auto co_main = start([]() {
    auto co = start([]() {
      dbg("Co1: Waiting for 1sec\n");
      wait(nullptr, 0, 1000);
      dbg("Co1: Waiting finished. Now self aborting\n");
      exitCo();
      dbg("Co1: This msg should not be printed\n");
    });
    dbg("Main will wait co\n");
    wait(co);
    dbg("Main goes on as co has been destroyed\n");
  });
}

void simpleWait() {
  dbg("Co1: Waiting for 2sec\n");
  wait(nullptr, 0, 1000);
  dbg("Co1: This msg should not be printed\n");
}


void test_co1_destroys_co2() {
  TSimpleDemo demo("test_co1_destroys_co2");
  auto co_main = start([]() {
    auto co1 = start(simpleWait);

    auto co2 = start([co1]() {
      dbg("Co2: Waiting for 1sec\n");
      wait(nullptr, 0, 1000);
      dbg("Co2: Waiting finished. Now destroying co1\n");
      exitCo(co1);
      dbg("Co2: co1 Destroyed\n");
      assert(!isHandle(co1));
      assert(isHandle(current()));
      wait(nullptr, 0, 500);
    });
    dbg("Main will wait co1 and co2\n");
    waitAll({ co1, co2 });
    dbg("Main goes on as co has been destroyed\n");
  });
}

// ----------------------------------------------------------
void sample_create() {
  for( int i=0; i<50; ++i )
    test_co1_destroys_co2();
  test_self_destroy();
  //test_create_from_co();
}
