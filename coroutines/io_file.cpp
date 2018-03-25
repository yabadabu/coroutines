#include "coroutines.h"
#include "io_file.h"

using namespace Coroutines;
using Coroutines::wait;

namespace Coroutines {

  namespace IO {

    namespace internal {

      enum eFileOpType { FILE_OP_READ, FILE_OP_WRITE };

      // A wrapper to automatically close the file on the dtor
      struct TFile {
        HANDLE handle;

        TFile(const char* filename, eFileOpType op_type ) {

          DWORD dwDesiredAccess = (op_type == FILE_OP_READ)
            ? GENERIC_READ
            : GENERIC_WRITE
            ;

          DWORD creationDisposition = (op_type == FILE_OP_READ)
            ? OPEN_EXISTING
            : CREATE_ALWAYS
            ;

          handle = ::CreateFileA(
            filename,		      // Name of the file
            dwDesiredAccess,	// Open for writing and reading
            0,								// Do not share
            nullptr,							// Default security
            creationDisposition,	// Always open
                                  // The file must be opened for asynchronous I/O by using the 
                                  // FILE_FLAG_OVERLAPPED flag.
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
            nullptr
          );

        }

        ~TFile() {
          if (isValid())
            ::CloseHandle(handle);
        }

        bool isValid() const {
          return handle != INVALID_HANDLE_VALUE;
        }

        size_t size() {
          LARGE_INTEGER sys_size;
          if (!::GetFileSizeEx(handle, &sys_size))
            return 0;
          return sys_size.QuadPart;
        }

      };

      // We have added to members
      struct CallbackInfo : OVERLAPPED {
        size_t bytes_to_process;
        bool   completed;
      };

      void WINAPI onCompleteFileOp(
        DWORD dwErr,
        DWORD cbBytesRead,
        LPOVERLAPPED lpOverLap
      )
      {
        auto p = (internal::CallbackInfo*)lpOverLap;
        assert(p);
        p->completed = (p->bytes_to_process == cbBytesRead);
      }

      static void checkIOCompletions() {
        // This allow the system to call our onCompleteFileOp
        ::WaitForSingleObjectEx(INVALID_HANDLE_VALUE, 0, true);
      }

      bool doAsyncFileOp(HANDLE h, void* buffer, size_t nbytes, eFileOpType file_op) {
        CallbackInfo cb;
        memset(&cb, 0x00, sizeof(cb));

        cb.OffsetHigh = 0;
        cb.Offset = 0;

        // Our params
        cb.bytes_to_process = nbytes;
        cb.completed = false;

        // Use os to perform the file op
        if (file_op == FILE_OP_READ)
          ::ReadFileEx(h, buffer, (DWORD)nbytes, &cb, &onCompleteFileOp);
        else
          ::WriteFileEx(h, buffer, (DWORD)nbytes, &cb, &onCompleteFileOp);

        DWORD err = GetLastError();
        if (err) {
          // We accept this error when writing
          if (file_op == FILE_OP_WRITE && err != ERROR_ALREADY_EXISTS)
            return false;
        }

        // Wait here until the requested file op has been completed
        Coroutines::wait([&cb]() {
          if( !cb.completed )
            checkIOCompletions();
          return !cb.completed;
        });

        return cb.completed;
      }

    }

    // Will return once all the file has been loaded
    bool loadFile(const char* filename, IO::TBuffer& buf) {
      using namespace internal;

      TFile f(filename, FILE_OP_READ);
      if (!f.isValid())
        return false;

      auto sz = f.size();
      if (!sz)
        return false;

      buf.resize(sz);

      return doAsyncFileOp(f.handle, buf.data(), buf.size(), FILE_OP_READ);
    }

    // Will return once all the file has been saved
    bool saveFile(const char* filename, const TBuffer& buf) {
      using namespace internal;

      TFile f(filename, FILE_OP_WRITE);
      if (!f.isValid())
        return false;

      return doAsyncFileOp(f.handle, (void*)buf.data(), buf.size(), FILE_OP_WRITE);
    }

  }

}
