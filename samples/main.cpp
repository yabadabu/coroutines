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
  TTimeDelta time_since_boot = Time::now() - boot_time;
  printf("[%s:%05x] %02d.%02d %s", Time::asStr(time_since_boot).c_str(), (int)getNumLoops(), current().id, current().age, buf);
#ifdef WIN32
  if( !strchr( buf, '\r') )
    ::OutputDebugStringA(buf);
#endif
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
extern void sample_sync();
extern void sample_go();
extern void sample_new_channels();
extern void sample_read_compress_write();

// -----------------------------------------------------------
int main(int argc, char** argv) {

#ifdef _WIN32
  WSADATA wsaData;
  int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

  dbg("sizeof(TWatchedEvent) = %ld\n", sizeof(TWatchedEvent));
  dbg("sizeof(THandle) = %ld\n", sizeof(THandle));
  dbg("sizeof(StrChan) = %ld\n", sizeof(StrChan));
  dbg("sizeof(TTimeStamp) = %ld\n", sizeof(TTimeStamp));
  

  boot_time = Time::now();
  //sample_wait();
  sample_channels();
  //sample_create();
  //sample_net();
  //sample_sync();
  //sample_go();
  //sample_new_channels();
  //sample_read_compress_write();
  return 0;
}

