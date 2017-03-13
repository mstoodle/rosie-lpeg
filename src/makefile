LIBNAME = lpeg
LUADIR = ../lua/
ROOTDIR = ../../
OMRDIR = $(ROOTDIR)/omr
JITBUILDER = $(OMRDIR)/jitbuilder/release/libjitbuilder.a

ifdef LPEG_DEBUG
COPT = -DLPEG_DEBUG -g
FILES = rcap.o rbuf.o lpvm.o lpcap.o lptree.o lpcode.o lpjit.o lpprint.o
else
COPT = -O2
FILES = rcap.o rbuf.o lpvm.o lpcap.o lptree.o lpcode.o lpjit.o
endif

ifdef ROSIE_DEBUG
COPT += -DROSIE_DEBUG
endif

CWARNS = -Wall -Wextra -pedantic \
	-Waggregate-return \
	-Wcast-align \
	-Wcast-qual \
	-Wdisabled-optimization \
	-Wpointer-arith \
	-Wshadow \
	-Wsign-compare \
	-Wundef \
	-Wwrite-strings \
	-Wbad-function-cast \
	-Wdeclaration-after-statement \
	-Wmissing-prototypes \
	-Wnested-externs \
	-Wstrict-prototypes \
        -Wunreachable-code \
        -Wno-missing-declarations \


CFLAGS = $(CWARNS) $(COPT) -std=c99 -I$(LUADIR)/include -fPIC -I$(OMRDIR)/jitbuilder/release/include
CC = gcc

default:
	@echo "Please specify a platform to build for.  Choices are: linux, macosx"
	@echo "Please first install omr using omr_macosx or omr_linux"

# For Linux
linux:
	make lpeg.so "DLLFLAGS = -shared -fPIC -L$(OMRDIR)/jitbuilder/release -ljitbuilder"

# For Mac OS
macosx: $(JITBUILDER)
	make lpeg.so "DLLFLAGS = -fno-rtti -fPIC -L$(OMRDIR)/jitbuilder/release -ljitbuilder -bundle -undefined dynamic_lookup"

lpeg.so: $(FILES) $(JITBUILDER)
	env $(CC) $(DLLFLAGS) $(FILES) -o lpeg.so

$(FILES): makefile

test: test.lua re.lua lpeg.so
	./test.lua

clean:
	rm -f $(FILES) lpprint.o lpeg.so


lpcap.o: lpcap.c lpcap.h rbuf.c rbuf.h rcap.c rcap.h lptypes.h 
lpcode.o: lpcode.c lptypes.h lpcode.h lptree.h lpvm.h lpcap.h
lpprint.o: lpprint.c lptypes.h lpprint.h lptree.h lpvm.h lpcap.h
lptree.o: lptree.c lptypes.h lpcap.h lpcode.h lptree.h lpvm.h lpprint.h
lpvm.o: lpvm.c lpcap.h lptypes.h lpvm.h lpprint.h lptree.h
rbuf.o: rbuf.c rbuf.h

lpjit.o: lpjit.cpp lpjit.hpp lpvm.h lpcap.h $(JITBUILDER)
	g++ -fno-rtti -fPIC -c -o $@ $(CWARNS) $(COPT) -I$(LUADIR) -I$(OMRDIR)/jitbuilder/release/include -I$(OMRDIR)/jitbuilder/release/include/compiler $<

omr_macosx:
	cd $(ROOTDIR) && git clone -b all_fixes https://github.com/mstoodle/omr.git && \
	cd omr && make -f run_configure.mk SPEC=osx OMRGLUE=./example/glue && \
	cd jitbuilder && make

omr_linux:
	cd $(ROOTDIR) && git clone -b all_fixes https://github.com/mstoodle/omr.git && \
	cd omr && make -f run_configure.mk SPEC=linux_x86-64 OMRGLUE=./example/glue && \
	cd jitbuilder && make
