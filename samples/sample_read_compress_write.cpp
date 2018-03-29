#include "sample.h"
#include "coroutines/io_file.h"

using namespace Coroutines;

// ---------------------------------------------------------
void test_read_compress_write() {
  TSimpleDemo demo("test_read_compress_write");

  typedef TTypedChannel<const char*> StrChan;
  typedef TTypedChannel<IO::TBuffer> BufferChan;

  auto files_to_load = StrChan::create(10);
  auto buffers = BufferChan::create(5);

  // This will queue some work to do
  auto c1 = start([files_to_load]() {
    files_to_load << "bigfile_00.dat";
    files_to_load << "bigfile_01.dat";
    files_to_load << "bigfile_02.dat";
    files_to_load << "bigfile_03.dat";
    close(files_to_load);
  });

  //               [         ]
  // scan files -> [ readers ] -> [ compress ] -> [ write ]
  //               [         ]
  // Here we read each file...
  auto c2 = start([files_to_load, buffers]() {

    const char* filename;
    while (filename << files_to_load) {
      IO::TBuffer buf;
      if (!IO::loadFile(filename, buf)) {
        dbg("Failed to load file %s\n", filename);
        continue;
      }
      dbg("file %s loaded %ld bytes\n", filename, buf.size());
      buffers << buf;
    }

    dbg("All files loaded\n");
    close(buffers);
  });

  auto c3 = start([buffers]() {
    int idx = 0;

    IO::TBuffer buf;
    while (buf << buffers) {
      
      char filename[64];
      snprintf(filename, sizeof( filename ), "out_%04d.dat", idx++);
      
      if( !IO::saveFile(filename, buf) ) {
        dbg("Failed to save file %s\n", filename);
        continue;
      }
      dbg("File %s saved with %ld bytes\n", filename, buf.size());
    }
    
    dbg("All files saved\n");
  });

}

// -----------------------------------------------------------
void sample_read_compress_write() {
  test_read_compress_write();
}
