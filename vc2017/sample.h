#pragma once

#include "coroutines/coroutines.h"

extern void dbg(const char *fmt, ...);
extern void runUntilAllCoroutinesEnd();

// -------------------------------------------------
#ifdef _WIN32

#define vsnprintf     _vsnprintf_s
#define sscanf        sscanf_s
#define isKeyPressed(x)  ((GetAsyncKeyState(x) & 0x8000) != 0)
#define keyBecomesPressed(x)  ((GetAsyncKeyState(x) & 0x0001) != 1)

#else

#define isKeyPressed(x)  ((rand() % 64) == 0)
#define keyBecomesPressed(x)  ((rand() % 64) == 0)

#endif

// -------------------------------------------------
struct TSimpleDemo {
  const char* title;
  TSimpleDemo(const char* new_title) : title(new_title) {
    dbg("-------------------------------\n%s starts\n", title);
  }
  ~TSimpleDemo() {
    dbg("%s waiting co's to finish\n", title);
    runUntilAllCoroutinesEnd();
    dbg("%s ends\n", title);
  }
};

