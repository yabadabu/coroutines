#include <cstdarg>
#include <cstdio>
#include <vector>
#include "sample.h"
#include "coroutines/io_channel.h"

using namespace Coroutines;
using Coroutines::wait;

THandle download(const char* url) {
  return start([url]() {

    CIOChannel conn;
    if (!conn.connect(url, 80, 1000))
      return;

    const char* request =
      "GET / HTTP/1.1\n"
      "User-Agent: Mozilla/4.0 (compatible; MSIE5.01; Windows NT)\n"
      "Accept-Language: en-us\n"
      "\n";
    if (!conn.send(request))
      return;

    while( true ) {
      char buf[1024];
      int bytes_recv = conn.recvUpTo(buf, sizeof(buf));
      if (bytes_recv > 0) {
        printf("Recv %s\n", buf);
      }
      else if (bytes_recv < 0)
        break;
    };
  });
}

// ---------------------------------------------------------
// Wait for any of the two coroutines to finish or timeout
void test_sync_parallels() {

  auto co = start([]() {

    auto f1 = download("www.google.com");
    auto f2 = download("www.github.com");
    auto f3 = download("www.amazon.com");
    wait({ f1,f2,f3 });

  });

}

// ----------------------------------------------------------
void sample_sync() {
  test_sync_parallels();
}
