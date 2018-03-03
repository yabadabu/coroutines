#pragma once

#include "coroutines/coroutines.h"

extern void dbg(const char *fmt, ...);
extern void runUntilAllCoroutinesEnd();

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

