This is a C++ framework to run coroutines using fcontext as a switching platform.

Status
------

- Runs on Windows (x64), Linux & OSX
- Currently just one thread does all the job without being blocked.
- Support for buffered channels with data types similar to go channels
- Support for timers and tickers as channels.
- Support for network (TCP ipv4 and ipv6)
- Support to load/save full files in async operations
- Wait for other coroutines, custom events, timeouts, channels, io events.
- Co's can wait for several mixed conditions
- You specify when can the coroutines run.

Install 
-------

  mkdir build  
  cmake -G "Visual Studio 15 2017 Win64" ..

Dependencies
------------

 - fcontext lib as switching platform
 - c++11 compiler

Quick Examples
--------------

Simple couroutine start and wait

```cpp
  // Simple create a coroutine
  auto co = start([](){
    printf( "Hi\n");
    wait( 2 * Time::Second );
    printf( "Bye\n");
  });

  // And wait from other...
  wait( co );
```

Channels example

```cpp
  // Because we can't wait from the main thread
  start([]() {
    // Create a channel to hold 32 ints
    auto ch1 = TTypedChannel<int>::create(32);
  
    // 3 consumers
    std::vector<THandle> consumers;
    for (int i = 0; i<3; ++i) {
      consumers.push_back( start([i, ch1]() {
        int id;
        while (id << ch1) {
          dbg("[%d] Consumed %d\n", i, id);
        };
        dbg("[%d] Leaves\n", i);
      }));
    }

    auto producer = start([ch1]() {
      int ids[] = { 2,3,5,7,11,13 };
      for (auto id : ids) {
        dbg("Producing %d\n", id);
        ch1 << id;
        wait(500 * Time::MilliSecond);
      }
      close(ch1);
    });

    waitAll(consumers, producer);
    dbg("All done\n");
  });
```

Network echo example client & server

```cpp
  void echoClient() {
    const char* addr = "127.0.0.1";
    Net::TSocket client = Net::connect(addr, port);
    if (!client)
      return;
    // Will send 5+1 bytes
    client << "Hello";
    char buf[256];
    int nbytes = Net::recvUpTo(client, buf, sizeof(buf));
    dbg("Server answer is %d bytes: %s\n", nbytes, buf);
    Net::close(client);
  }

  void echoServer(Net::TSocket server) {
    auto client = Net::accept(server);
    if (!client)
      return;
    char buf[256];
    int nbytes = Net::recvUpTo(client, buf, sizeof(buf));
    Net::send(client, buf, nbytes);
    Net::close(client);
  }

  // ----------------------------------------------------------
  auto co_s = start([]() {
    auto server = Net::listen("127.0.0.1", port, AF_INET);
    if (!server)
      return;
    while (true) {
      echoServer(server);
      break;              // Exit after one client received
    }
    Net::close(server);
  });

  auto co_c = start(echoClient);
```

Syntax to wait for several events.

```cpp
    // When a co wants to 'wait' for activity in two channels with a timer...
    TWatchedEvent wes[3] = {
      canRead( channel1 ), 
      canRead( channel2 ),
      400 * MilliSecond
    };
    int n = wait( wes, 3 );  // Return index of the event triggered
```

Or use the 'choose' syntax if you want to have the reaction of the event close to the event def.

```cpp
    int n = choose( 
      ifCanRead( channel1, []( const char* data ){
        // Do something with char* data...
      })
      ,ifCanRead( channel2, []( int data ){
        // Do something with int data...
      })
      ,ifTimeout(400 * MilliSecond, []() {
        // Timeout waiting...
      })
    );
```

TODO
----

- [ ] There is no option to destroy a channel. Channels should autodestroy
  when empty() and closed(). Alarms when fired?
- [ ] Choose on null handles should never block
- [ ] Add more examples
- [ ] Remove old entries from 'VDescriptors' at internal::TIOEvents once done
- [ ] Wait for Barrier or semaphores.
- [ ] Test poll, epoll
- [ ] Add HTTP download
- [ ] Add support for https
- [x] Add io channels to use in the choose
- [x] Using std::chrono
- [x] Increase timer resolution to usecs
- [x] Close channel to ChanHandle
- [x] Wait for async file reads.
- [x] Remove old channels
- [x] Move TChannel/CIOHandle to handle-based resource or simple identifiers
- [x] Allow user to create his own events, which when triggered will wake up
- [x] Support for IPv6
- [x] Test in unix/osx 
- [x] Confirm a co can abort itself, or another co.
- [x] Use proper real time for wait fn's. We have millisecond precision

Behaviour
---------

- A co is:
  - Running, meaning will run until yields or faces a wait condition
  - We can wait because:
    - We want to read from a channel and there is no data in the channel
    - We want to write to a channel, but there is no space left in the channel
    - We want to wait for another co to finish
    - We just want to wait some time (microsecs resolution)
    - Waiting for user defined conditions which need to be checked/poll on every 
      tick
    - Waiting for Net operations
    - We want to wait until some user generated events is set.
      Setting the event will wake up me
- Other rules
    - When a channel has 1 data and there is more than one co waiting for 
        that data, only the first which entered sleep mode will be awaken. The
        other will remain in sleep mode
    - A co can exit itself
    - A co can create other co
    - The main thread can't enter a wait state. You must be inside a co

- Implications
  - A channel must known which co's are waiting for data in the channel
  - A channel must known which co's are waiting for space in the channel
  - A co must known who is waiting for him. And wake up when dies.
  - There is a list of co waiting for time events
  - A co could be waiting for several channels events and a timeout
  - A co knows which other co's are waiting for me. Destroying myself
    will wake up those co's.
  - Event watchers: The co's
    - Need to know to which event sources they are subscribed
    - Need to be able to unsubscribe from all their event sources

  - If the 'watched event' is created in the stack, we don't need to malloc/free that memory
    Just be sure the events are unregistered when the thread continues
  - Add a next/prev link so the events can be anywhere

