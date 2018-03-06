#define _CRT_SECURE_NO_WARNINGS
#include <cstdarg>
#include <cstdio>
#include "sample.h"

using namespace Coroutines;

TTimeStamp boot_time;

// --------------------------------------------------
void dbg(const char *fmt, ...) {
  char buf[1024];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
  if (n < 0)
    buf[1023] = 0x00;
  va_end(ap);
  TTimeStamp time_since_boot = now() - boot_time;
  long nsecs, nmsecs;
  getSecondsAndMilliseconds(time_since_boot, &nsecs, &nmsecs);
  printf("[%04d:%03d:%05x] %02d.%02d %s", (int)nsecs, (int)nmsecs, (int)getNumLoops(), current().id, current().age, buf);
}

void runUntilAllCoroutinesEnd() {
  int counter = 0;
  while (true) {
    if (!executeActives())
      break;
    dbg("%d\r", counter++);
  }
  dbg("all done after %d iters\n", counter);
}

// ---------------------------------------------------------------
// -----------------------------------------------------------
extern void sample_channels();
extern void sample_net();
extern void sample_create();
extern void sample_wait();

// -----------------------------------------------------------
int main(int argc, char** argv) {
  boot_time = now();
  //sample_wait();
  //sample_channels();
  //sample_create();
  sample_net();
  return 0;
}

