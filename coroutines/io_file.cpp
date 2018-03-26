#include "coroutines.h"
#include "io_file.h"

extern void dbg(const char *fmt, ...);

// This is our common interface for both Windows and Unix
namespace Coroutines {

  namespace IO {

    namespace internal {

      // A wrapper to automatically close the file on the dtor
      struct TFile {

#ifdef WIN32
        HANDLE handle;
#else
        int    handle;
#endif

        enum  eMode { FOR_READING, FOR_WRITING };
        eMode mode;

        TFile(const char* filename, eMode new_mode);
        ~TFile();

        bool isValid() const;
        size_t size() const;
        bool asyncRead(void* data, size_t nbytes);
        bool asyncWrite(const void* data, size_t nbytes);
      };

    } // internal
  } // IO
} // Coroutines

#ifndef WIN32

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

namespace Coroutines {

  namespace IO {

    namespace internal {

      static bool setNonBlocking(int handle) {
        int flags = fcntl(handle, F_GETFL, 0);
        if (flags == -1)
          flags = 0;
        auto rc = fcntl(handle, F_SETFL, flags | O_NONBLOCK);
        if (rc != 0)
          dbg("Failed to set socket %d as non-blocking\n", handle);
        return rc == 0;
      }

      TFile::TFile(const char* filename, eMode new_mode ) {
        mode = new_mode;
        int flags = ( new_mode == FOR_READING ) ? O_RDONLY : ( O_WRONLY | O_CREAT );
        handle = ::open( filename, flags );
        if( isValid() )
          setNonBlocking(handle);
      }

      TFile::~TFile() {
        if (isValid())
          ::close(handle);
      }

      bool TFile::isValid() const {
        return handle != -1;
      }

      size_t TFile::size() const {
        struct stat buf;
        int rc = fstat( handle, &buf );
        return (rc == 0) ? buf.st_size : 0;
      }

      bool TFile::asyncRead( void* data, size_t nbytes ) {
        assert( mode == FOR_READING && isValid() );
        size_t total_bytes = 0;
        while( isValid() ) {
          auto rc = ::read( handle, (char*) data + total_bytes, nbytes - total_bytes );
          if( rc == -1 ) {
            if( errno == EAGAIN ) {
              //dbg( "read returned EAGAIN\n");
              TWatchedEvent we(handle, EVT_SOCKET_IO_CAN_READ);
              wait(&we, 1);
            }
            else 
              break;
          } else {
            total_bytes += rc;
            //dbg( "read %ld => %ld/%ld\n", rc, total_bytes, nbytes);
            if( total_bytes == nbytes )
              break;
          }
        }
        return total_bytes == nbytes;
      }

      bool TFile::asyncWrite( const void* data, size_t nbytes ) {
        assert( mode == FOR_WRITING && isValid() );
        size_t total_bytes = 0;
        while( isValid() ) {
          auto rc = ::write( handle, (char*) data + total_bytes, nbytes - total_bytes );
          if( rc == -1 ) {
            if( errno == EAGAIN ) {
              //dbg( "write returned EAGAIN\n");
              TWatchedEvent we(handle, EVT_SOCKET_IO_CAN_WRITE);
              wait(&we, 1);
            }
            else 
              break;
          } else {
            total_bytes += rc;
            //dbg( "write %ld => %ld/%ld\n", rc, total_bytes, nbytes);
            if( total_bytes == nbytes )
              break;
          }
        }
        return total_bytes == nbytes;
      }

    }
  }
}

#else 

// Implementation for Windows

namespace Coroutines {

  namespace IO {

    namespace internal {

      // A wrapper to automatically close the file on the dtor
      TFile::TFile(const char* filename, eMode new_mode) {
        mode = new_mode;

        DWORD dwDesiredAccess = (mode == FOR_READING)
          ? GENERIC_READ
          : GENERIC_WRITE
          ;

        DWORD creationDisposition = (mode == FOR_READING)
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

      TFile::~TFile() {
        if (isValid())
          ::CloseHandle(handle);
      }

      bool TFile::isValid() const {
        return handle != INVALID_HANDLE_VALUE;
      }

      size_t TFile::size() const {
        LARGE_INTEGER sys_size;
        if (!::GetFileSizeEx(handle, &sys_size))
          return 0;
        return sys_size.QuadPart;
      }

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

      static bool doAsyncFileOp(HANDLE h, void* buffer, size_t nbytes, TFile::eMode mode) {
        CallbackInfo cb;
        memset(&cb, 0x00, sizeof(cb));

        cb.OffsetHigh = 0;
        cb.Offset = 0;

        // Our params
        cb.bytes_to_process = nbytes;
        cb.completed = false;

        // Use os to perform the file op
        if (mode == TFile::FOR_READING)
          ::ReadFileEx(h, buffer, (DWORD)nbytes, &cb, &onCompleteFileOp);
        else
          ::WriteFileEx(h, buffer, (DWORD)nbytes, &cb, &onCompleteFileOp);

        DWORD err = GetLastError();
        if (err) {
          // We accept this error when writing
          if (mode == TFile::FOR_WRITING && err != ERROR_ALREADY_EXISTS)
            return false;
        }

        // Wait here until the requested file op has been completed
        Coroutines::wait([&cb]() {
          if (!cb.completed)
            checkIOCompletions();
          return !cb.completed;
        });

        return cb.completed;
      }

      bool TFile::asyncRead(void* buffer, size_t nbytes) {
        return doAsyncFileOp(handle, buffer, nbytes, mode);
      }

      bool TFile::asyncWrite(const void* buffer, size_t nbytes) {
        return doAsyncFileOp(handle, (void*)buffer, nbytes, mode);
      }
    }
  }
}

#endif

namespace Coroutines {

  namespace IO {

    // This is the implementation of the common interface
    using namespace internal;

    // -------------------------------------------------------------- 
    bool loadFile(const char* filename, IO::TBuffer& buf) {

      TFile f(filename, TFile::FOR_READING);
      if (!f.isValid())
        return false;

      auto sz = f.size();
      if (!sz)
        return false;

      buf.resize(sz);

      return f.asyncRead(buf.data(), buf.size());
    }

    // -------------------------------------------------------------- 
    bool saveFile(const char* filename, const IO::TBuffer& buf) {

      TFile f(filename, TFile::FOR_WRITING);
      if (!f.isValid())
        return false;

      return f.asyncWrite(buf.data(), buf.size());
    }

  }

}

