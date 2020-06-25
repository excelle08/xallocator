AR=ar
CC=g++
override CFLAGS += '-I./include'

OBJS = Allocator.o	\
	   Fault.o		\
	   xallocator.o

all: xallocator

xallocator: $(OBJS)
	$(AR) rvs $@.a $^
	$(CC) -shared -o lib$@.so -Wl,--whole-archive $@.a -Wl,--no-whole-archive
	
%.o : src/%.cpp
	$(CC) -c $< -o $@ $(CFLAGS) -fPIC

install: xallocator
	cp -R ./include /usr/local/include/xalloc
	cp lib$^.so /usr/local/lib
	ldconfig

demo: xallocator demo/main.cpp
	$(CC) -o main demo/main.cpp $(CFLAGS) -L./ -lxallocator 

cleantmp:
	rm *.o *.a

clean:
	rm -f *.o *.so main *.a
