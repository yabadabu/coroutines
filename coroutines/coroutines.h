#ifndef INC_JABA_COROUTINES_H_
#define INC_JABA_COROUTINES_H_

#include <cinttypes>
#include <cassert>
#include <cstring>
#include <functional>

namespace Coroutines {

  // --------------------------------------------------
  typedef uint64_t      u64;
  typedef uint8_t       u8;

  struct THandle;
  struct TWatchedEvent;

  // --------------------------------------------------
  struct THandle {
    uint16_t id = 0;
    uint16_t age = 0;
    bool operator==(const THandle& other) const { return id == other.id && age == other.age; }
    uint32_t asUnsigned() const { return (age << 16 ) | id; }
  };

  typedef std::function<void(void)> TBootFn;

  // --------------------------
  THandle start(TBootFn&& user_fn);

  // --------------------------------------------------
  bool    isHandle(THandle h);
  THandle current();
  void    yield();
  void    exitCo(THandle h = current());
  
  // --------------------------------------------------
  int     executeActives();
	size_t  getNumLoops();

  // --------------------------------------------------
  void    wakeUp(TWatchedEvent* we);
  typedef std::function<bool(void)> TWaitConditionFn;

}

#include "channel_handle.h"
#include "list.h"
#include "timeline.h"
#include "io_events.h"
#include "events.h"
#include "wait.h"
#include "channel.h"

#endif
