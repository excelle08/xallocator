# xallocator - A fast fixed block memory allocator

This memory allocator is authored by David Lafreniere
([Replace malloc/free with a Fast Fixed Block Memory Allocator][1]).
I revised the code a little bit to make it compatible with POSIX
environment, reorganized it and created a Makefile for it to generate
a shared library `libxallocator.so`.

## Usage

* Build the shared library: `make`

* Clean up intermediate files: `make cleantmp`

* Build the demo: `make demo`. Then the demo program `main` will appear
  in the repo directory. Note that currently the benchmarks are only
  runnable on Windows.

* Install the library: `sudo make install`. The headers will be copied
  to `/usr/local/include/xalloc`.

* Remove the library: Remove `/usr/local/lib/libxallocator.so` and
  `/usr/local/include/xalloc`

[1]: https://www.codeproject.com/Articles/1084801/Replace-malloc-free-with-a-Fast-Fixed-Block-Memory

