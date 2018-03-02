#define _CRT_SECURE_NO_WARNINGS
#include <cstdarg>
#include <cstdio>
#include <vector>
#include "coroutines/coroutines.h"

using namespace Coroutines;

extern void dbg(const char *fmt, ...);
extern void waitKeyPress(int c);

void waitKeyPress(int c) {
  wait([c]() { 
    return (::GetAsyncKeyState(c) & 0x0001) == 0; 
  });
}


// ---------------------------------------------------------
static void doSecond() {
  int key = 'W';
  int n = 0;
  int max_n = 3;
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

    start(&doSecond);

  }

  dbg("doSpawner: ends\n");
}

// ----------------------------------------------------------
void sample_create() {

  auto co_s = start( &doSpawner);

  int counter = 0;
  while (true) {
    Coroutines::updateCurrentTime(1);
    if (!Coroutines::executeActives())
      break;
    dbg("%d\r", counter++);
  }
  dbg("sample_create done\n");
}