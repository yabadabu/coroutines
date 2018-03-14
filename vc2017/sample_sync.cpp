#include <cstdarg>
#include <cstdio>
#include <vector>
#include "sample.h"
#include "coroutines/io_channel.h"

using namespace Coroutines;
using Coroutines::wait;

namespace HTTP {

  // --------------------------------------------------
  struct TRequest {

    int         port = 80;
    std::string uri;        // Given by the user
    std::string host;
    std::string path = "/";
    std::string protocol_version = "HTTP/1.1";
    std::string method = "GET";
    std::string agent = "Mozilla/4.0 (compatible; MSIE5.01; Windows NT)";
    std::string request;

    bool setURI(const std::string& new_uri) {
      uri = new_uri;
      // Super baseic, will not work if uri starts with http://
      auto path_idx = uri.find('/');
      if (path_idx != std::string::npos)
        path = uri.substr(path_idx);

      host = uri.substr(0, path_idx);
    
      buildRequest();
      return true;
    }

    void buildRequest() {
      request = method + " " + path + " " + protocol_version + "\n";
      request += "User-Agent: " + agent + "\n";
      request += "Host: " + host + "\n";
      request += "Accept-Language: en-us\n";
      request += "\n";
    }

  };

  // --------------------------------------------------
  struct TAnswer {
    size_t                 content_size = 0;
    bool                   header_found = false;
    std::string            header;
    std::vector< uint8_t > answer;

    bool findContentSizeInHeader() {
      int header_content_size = 0;
      auto c = header.find("Content-Length:");
      if (c == std::string::npos)
        return false;

      sscanf_s(header.data() + c + 16, "%ld", &header_content_size);
      dbg("Content size is %d\n", header_content_size);
      content_size = header_content_size;
      return true;
    }

    bool parseHeader() {

      if (answer.size() < 4)
        return false;

      // Scan for header/body separator
      auto b_of_header = answer.data();
      auto b = b_of_header + 3;
      
      bool found = false;
      while (b < b_of_header + answer.size()) {
        if (b[-3] == '\r' && b[-2] == '\n' && b[-1] == '\r' && b[0] == '\n') {
          found = true;
          break;
        }
        b++;
      }

      if (!found) 
        return false;

      header_found = true;
      size_t header_size = b - b_of_header + 1;
      header = std::string((const char*) b_of_header, header_size);
      dbg("%s\n", header.c_str());

      findContentSizeInHeader();

      // Remove the header of our answer buffer
      answer.erase(answer.begin(), answer.begin() + header_size);
      return true;
    }

    // Will return true if we know the content size and have received
    // that size as part of the content
    bool parse(const uint8_t* buf, size_t buf_size) {
    
      // Append to our current buffer
      answer.insert(answer.end(), buf, buf + buf_size);
    
      // If we have not parsed the header yet...
      if (!header_found) {
        // Need more data if we can't identify the header 
        if( !parseHeader() )
          return false;
      }

      // At this point if content size is still zero it means we can't know the
      // size of the contents. So we better abort.
      if (!content_size)
        return true;

      return (content_size == answer.size());
    }
  };

}

struct TDownloadTask {
  const char*            uri;
  HTTP::TAnswer          answer;
  TTimeStamp             ts_start = 0;
  TTimeDelta             time_to_connect = 0;
  TTimeDelta             time_to_download = 0;
  TTimeDelta             time_task = 0;
  TTimeStamp             ts_end = 0;
  TDownloadTask(const char* new_uri)
    : uri(new_uri) {
  }
};

bool download(TDownloadTask* dt) {
  
  dbg("Starting download %s\n", dt->uri);

  HTTP::TRequest r;
  r.setURI(dt->uri);

  dt->ts_start = now();

  // Connect
  CIOChannel conn;
  if (!conn.connect(r.host.c_str(), r.port, 1000)) {
    dbg("Failed to connect to uri %s\n", dt->uri);
    return false;
  }
  dt->time_to_connect = now() - dt->ts_start;

  // Build and sent request
  if (!conn.send(r.request.c_str(), r.request.length()))
    return false;

  // Recv answer
  TTimeStamp ts_download_start = now();
  while( true ) {
    // Save downloaded chunk in a tmp buffer
    uint8_t buf[8192];
    int bytes_recv = conn.recvUpTo(buf, sizeof(buf));
    if (bytes_recv > 0) {
      dbg("Recv %ld bytes for %s\n", bytes_recv, dt->uri);
      // Let the answer parse the buffer
      if (dt->answer.parse(buf, bytes_recv))
        break;
    }
    else if (bytes_recv == 0)
      break;
    else if (bytes_recv <= 0) {
      dbg("Error detected while downloading the file %s\n", dt->uri);
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
void test_download_in_parallel() {
  TSimpleDemo demo("test_download_in_parallel");

  auto ch_requests = new TChannel<TDownloadTask*>(10);
  auto ch_acc = new TChannel<TDownloadTask*>(10);
  bool      all_queued = false;
  int       ndownloads = 0;

  // Generate the requests from another co with some in the middle waits
  auto co_producer = start([ch_requests, &all_queued, &ndownloads]() {
    ch_requests->push(new TDownloadTask("www.lavanguardia.com")); ++ndownloads;
    wait(nullptr, 0, 100);
    ch_requests->push(new TDownloadTask("blog.selfshadow.com")); ++ndownloads;
    ch_requests->push(new TDownloadTask("www.humus.name/index.php?page=News")); ++ndownloads;
    wait(nullptr, 0, 100);
    ch_requests->push(new TDownloadTask("www.humus.name/index.php?page=3D")); ++ndownloads;
    all_queued = true;
  });

  // Let's start some coroutines, each one will ...
  int n_downloaders = 2;
  for (int i = 0; i < n_downloaders; ++i) {
    auto h = start([ch_requests, ch_acc]() {
      // Take a download task from the channel while the channel is alive
      TDownloadTask* dt = nullptr;
      while (ch_requests->pull(dt)) {
        // Download it and..
        download(dt);
        // Queue to the next stage
        ch_acc->push(dt);
      }
    });
  }

  // Create another task to sumarize the data
  auto h_ac = start([ch_requests, ch_acc, &all_queued, &ndownloads]() {

    TTimeStamp ts_start = 0;
    TTimeStamp ts_end = 0;
    size_t ntasks = 0;
    size_t total_bytes = 0;
    TDownloadTask* dt = nullptr;
    while ((ntasks < ndownloads || !all_queued ) && ch_acc->pull(dt)) {
      assert(dt);

      // Accumulate some total bytes and max time
      total_bytes += dt->answer.content_size;

      if (dt->ts_start < ts_start || ts_start == 0)
        ts_start = dt->ts_start;

      if (dt->ts_end > ts_end || ts_end == 0)
        ts_end = dt->ts_end;

      ++ntasks;
      dbg("Task %ld/%ld completed %s\n", ntasks, ndownloads, dt->uri);
    }

    dbg("Total bytes downloaded %ld using %ld tasks\n", total_bytes, ntasks);
    
    long secs, msecs;
    getSecondsAndMilliseconds(ts_end - ts_start, &secs, &msecs);
    dbg("Total Required time: %ld:%ld\n", secs, msecs);

    // Closing the channel will trigger the end of the coroutines waiting for more data
    ch_requests->close();

    // This is for cleanup
    ch_acc->close();
  });

}

// ----------------------------------------------------------
void sample_sync() {
  test_download_in_parallel();
}
