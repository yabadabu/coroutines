#ifndef INC_COROUTINES_IO_FILE_H_
#define INC_COROUTINES_IO_FILE_H_

#include <vector>

namespace Coroutines {

  namespace IO {

    typedef std::vector< uint8_t > TBuffer;
    bool loadFile(const char* filename, TBuffer& buf);
    bool saveFile(const char* filename, const TBuffer& buf);

  }

}


#endif
