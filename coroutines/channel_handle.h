#ifndef INC_COROUTINES_CHANNEL_HANDLE_H_
#define INC_COROUTINES_CHANNEL_HANDLE_H_

#include <cinttypes>

namespace Coroutines {

  // -------------------------------------------------------
  // -------------------------------------------------------
  // -------------------------------------------------------
  struct TChanHandle {
    enum eClassID { CT_INVALID = 0, CT_TIMER = 1, CT_MEMORY, CT_IO };
    
    eClassID     class_id : 4;
    uint32_t     index : 12;
    uint32_t     age : 16;

    TChanHandle() {
      class_id = CT_INVALID;
      index = age = 0;
    }

    TChanHandle(eClassID channel_type, int32_t new_index) {
      class_id = channel_type;
      index = new_index;
      age = 1;
    }
    
    bool operator==(const TChanHandle h) const {
      return h.class_id == class_id && h.index == index && h.age == age;
    }

  };

  static_assert(sizeof(TChanHandle) == sizeof(uint32_t), "TChanHandle should have size of int32_t");

}

#endif
