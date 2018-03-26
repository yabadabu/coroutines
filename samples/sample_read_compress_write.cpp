#include "sample.h"
#include "coroutines/io_file.h"

using namespace Coroutines;

typedef TTypedChannel<IO::TBuffer> BufferChan;

// ---------------------------------------------------------
void test_write_async() {
  TSimpleDemo demo("test_write_async");

	auto c1 = start([&]() {
		IO::TBuffer b;
		b.resize( 20 * 1024 * 1024 );
		dbg( "Filling buffer\n" );
		for( size_t i=0; i<b.size(); ++i ) 
			b[i] = rand() & 255;

		dbg( "Starting save of buffer with %ld Kbytes\n", b.size() / 1024 );
		IO::saveFile( "random.dat", b );
		dbg( "saveFile of %ld bytes completed\n", b.size() );

		// Num Loops should be way bigger than zero
		assert( getNumLoops() > 50 );
	});

}

// ---------------------------------------------------------
void test_write_in_parallel() {
  TSimpleDemo demo("test_in_parallel");

  auto buffers = BufferChan::create(5);
  
	auto c1 = start([&]() {
		// Fill n buffers
		for( int i=0; i<4; ++i ) {
			IO::TBuffer b;
			b.resize( ( 10 + i ) * 1024 * 1024 );
			dbg( "Filling buffer\n" );
			for( size_t i=0; i<b.size(); ++i ) 
				b[i] = rand() & 255;
			buffers << b;
			Time::sleep( 1 );
		}
		close( buffers );
	});

	int nsavers = 1;

	for( int i=0; i<nsavers; ++i ) {
		auto ci = start([c1, buffers]() {

			IO::TBuffer b;
		  while( b << buffers ) {
				dbg( "Starting save of buffer with %ld bytes\n", b.size() );
				static int idx = 0;
				char filename[256];
				sprintf( filename, "random_%02d.dat", idx++ );
				IO::saveFile( filename, b );
				dbg( "saveFile of %ld Kbytes completed\n", b.size() / 1024 );
	  	
				//Time::sleep( 5 );
				yield();

			}
		});
	}

}

// ---------------------------------------------------------
void test_read_compress_write() {
  TSimpleDemo demo("test_read_compress_write");

  typedef TTypedChannel<const char*> StrChan;

  auto files_to_load = StrChan::create(10);
  auto buffers = BufferChan::create(5);
	int nreaders = 4;

  // This will queue some work to do
  auto c1 = start([files_to_load]() {
    files_to_load << "test";
    files_to_load << "sample_wait.o";
    files_to_load << "BezelSources.7z";
    files_to_load << "microprofile.zip";
    close(files_to_load);
  });

  //               [         ]
  // scan files -> [ readers ] -> [ compress ] -> [ write ]
  //               [         ]
  // Here we read each file...
	for( int i=0; i<nreaders; ++i ) {

		auto c2 = start([files_to_load, buffers]() {

			const char* filename;
			while (filename << files_to_load) {
				IO::TBuffer buf;
				dbg("Loading file %s\n", filename );
				if (!IO::loadFile(filename, buf)) {
					dbg("Failed to load file %s\n", filename);
					continue;
				}
				dbg("file %s loaded %ld bytes\n", filename, buf.size());
				buffers << buf;
			}

			if( close(buffers) )
				dbg("All files loaded\n");
		});

	}

  auto c3 = start([buffers]() {
    int idx = 0;

    IO::TBuffer buf;
    while (buf << buffers) {
      
      char filename[64];
      snprintf(filename, sizeof( filename ), "out_%04d.dat", idx++);
    	dbg("Saving file %s\n", filename );
      
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
  //test_read_compress_write();
  //test_write_async();
  test_write_in_parallel();
}
