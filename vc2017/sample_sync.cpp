#include <cstdarg>
#include <cstdio>
#include <vector>
#include "sample.h"
#include "coroutines/io_channel.h"

using namespace Coroutines;
using Coroutines::wait;

struct TDownloadTask {
  const char*            url;
  std::vector< uint8_t > data;
  TTimeStamp             ts_start = 0;
  TTimeDelta             time_to_connect = 0;
  TTimeDelta             time_to_download = 0;
  TTimeDelta             time_task = 0;
  TTimeStamp             ts_end = 0;
  TDownloadTask(const char* new_url)
    : url(new_url) {
  }
};

bool download(TDownloadTask* dt) {
  
  dbg("Starting download %s\n", dt->url);

  char host[512];
  strcpy_s(host, dt->url);

  const char* uri = strchr(dt->url, '/');
  if (!uri) {
    uri = "/";
  }
  else {
    host[uri - dt->url] = 0x00;
  }

  dt->data.clear();

  dt->ts_start = now();

  // Connect
  CIOChannel conn;
  if (!conn.connect(host, 80, 1000)) {
    dbg("Failed to connect to url %s\n", dt->url);
    return false;
  }
  dt->time_to_connect = now() - dt->ts_start;

  // Build and sent request
  char request[512];
  snprintf( request, sizeof(request)-1,
    "GET %s HTTP/1.1\n"
    "User-Agent: Mozilla/4.0 (compatible; MSIE5.01; Windows NT)\n"
    "Host: %s\n"
    "Accept-Language: en-us\n"
    "\n",
    uri, 
    host);
  if (!conn.send(request, strlen( request )))
    return false;

  size_t expected_content_size = 0;

  // Recv answer
  TTimeStamp ts_download_start = now();
  while( true ) {
    uint8_t buf[4096];
    int bytes_recv = conn.recvUpTo(buf, sizeof(buf));
    if (bytes_recv > 0) {
      dbg("Recv %ld bytes for %s\n", bytes_recv, dt->url);
      dt->data.insert(dt->data.end(), buf, buf + bytes_recv);
      if (!expected_content_size) {

        // Scan for header/body separator
        auto b_of_header = dt->data.data();
        auto b = b_of_header;
        while (b - b_of_header < (dt->data.size() - 3 )) {
          if (b[0] == '\r' && b[1] == '\n' && b[2] == '\r' && b[3] == '\n')
            break;
          b++;
        }
        char header[4096];
        size_t header_size = b - b_of_header + 4;
        memcpy(header, b_of_header, header_size);
        header[header_size] = 0x00;
        dbg("%s\n", header);

        int header_content_size = 0;
        auto c = strstr(header, "Content-Length:");
        if (c) {
          sscanf_s(c + 16, "%ld", &header_content_size);
          dbg("Content size is %d\n", header_content_size);
          expected_content_size = header_content_size;
        }

        // Failed to get content_size from the header.
        if (!expected_content_size)
          break;

        dt->data.erase(dt->data.begin(), dt->data.begin() + header_size);
      }

      if (expected_content_size == dt->data.size())
        break;
    }
    else if (bytes_recv == 0)
      break;
    else if (bytes_recv <= 0) {
      dbg("Error detected while downloading the file %s\n", dt->url);
      break;
    }
  };
  dt->time_to_download = now() - ts_download_start;
  
  // Total time
  dt->ts_end = now();
  dt->time_task = dt->ts_end - dt->ts_start;

  conn.close();

  return true;
}

// ---------------------------------------------------------
// Wait for any of the two coroutines to finish or timeout
void test_sync_parallels() {
  TSimpleDemo demo("test_sync_parallels");

  TChannel* ch_requests = new TChannel(10, sizeof(TDownloadTask*));
  TChannel* ch_acc = new TChannel(10, sizeof(TDownloadTask*));
  push(ch_requests, new TDownloadTask("www.lavanguardia.com"));
  push(ch_requests, new TDownloadTask("blog.selfshadow.com"));
  push(ch_requests, new TDownloadTask("www.humus.name/index.php?page=News"));
  push(ch_requests, new TDownloadTask("www.humus.name/index.php?page=3D"));

  size_t ntasks_to_download = ch_requests->size();

  // Let's start some coroutines, each one will ...
  int n_downloaders = 2;
  for (int i = 0; i < n_downloaders; ++i) {

    auto h = start([ch_requests, ch_acc]() {

      // Take a download task from the channel
      TDownloadTask* dt = nullptr;
      while (pull(ch_requests, dt)) {
        // Download it and..
        download(dt);
        // Queue to the next stage
        push(ch_acc, dt);
      }

    });

  }

  // Create another task to sumarize the data
  auto h_ac = start([ch_requests, ch_acc, ntasks_to_download]() {

    TTimeStamp ts_start = 0;
    TTimeStamp ts_end = 0;
    size_t ntasks = 0;
    size_t total_bytes = 0;
    TDownloadTask* dt = nullptr;
    while (ntasks < ntasks_to_download && pull(ch_acc, dt)) {
      assert(dt);

      total_bytes += dt->data.size();

      if (dt->ts_start < ts_start || ts_start == 0)
        ts_start = dt->ts_start;

      if (dt->ts_end > ts_end || ts_end == 0)
        ts_end = dt->ts_end;

      ++ntasks;
      dbg("Task %ld/%ld completed %s\n", ntasks, ntasks_to_download, dt->url);
    }

    dbg("Total bytes downloaded %ld using %ld tasks\n", total_bytes, ntasks);
    
    long secs, msecs;
    getSecondsAndMilliseconds(ts_end - ts_start, &secs, &msecs);
    dbg("Total Required time: %ld:%ld\n", secs, msecs);

    // Closing the channel will trigger the end of the coroutines waiting for more data
    ch_requests->close();
  });

}

// ----------------------------------------------------------
void sample_sync() {
  test_sync_parallels();
}
