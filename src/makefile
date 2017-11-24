LIBNAME = lpeg
LUADIR = ../lua/
ROOTDIR = ../../
OMRDIR = $(ROOTDIR)/omr
JITBUILDER = $(OMRDIR)/jitbuilder/release/libjitbuilder.a

ifdef LPEG_DEBUG
COPT = -DLPEG_DEBUG -g
FILES = rcap.o rbuf.o lpvm.o lpcap.o lptree.o lpcode.o lpprint.o
else
COPT = -O2
FILES = rcap.o rbuf.o lpvm.o lpcap.o lptree.o lpcode.o
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
	@echo "Please first install omr using the omr target

# For Linux
linux:
	make lpeg.so "DLLFLAGS = -shared -fPIC"
	make lpjit.so "DLLFLAGS = -shared -rdynamic -fPIC -fno-rtti -L$(OMRDIR)/jitbuilder/release -ljitbuilder"

# For Mac OS
macosx:
	make lpeg.so "DLLFLAGS = -fno-rtti -rdynamic -fPIC -bundle -undefined dynamic_lookup"
	make lpjit.so "DLLFLAGS = -fno-rtti -rdynamic -fPIC -L$(OMRDIR)/jitbuilder/release -ljitbuilder -bundle -undefined dynamic_lookup"

lpeg.so: $(FILES)
	env $(CC) $(DLLFLAGS) $(FILES) -o lpeg.so

lpjit.so: lpjit.o $(JITBUILDER)
	env g++ $(DLLFLAGS) -o $@ $<

$(FILES): makefile

test: test.lua re.lua lpeg.so
	./test.lua

clean:
	rm -f $(FILES) lpprint.o lpjit.o lpeg.so lpjit.so


lpcap.o: lpcap.c lpcap.h rbuf.c rbuf.h rcap.c rcap.h lptypes.h 
lpcode.o: lpcode.c lptypes.h lpcode.h lptree.h lpvm.h lpcap.h
lpprint.o: lpprint.c lptypes.h lpprint.h lptree.h lpvm.h lpcap.h
lptree.o: lptree.c lptypes.h lpcap.h lpcode.h lptree.h lpvm.h lpprint.h
lpvm.o: lpvm.c lpcap.h lptypes.h lpvm.h lpprint.h lptree.h
rbuf.o: rbuf.c rbuf.h

lpjit.o: lpjit.cpp lpjit.h lpvm.h lpcap.h $(JITBUILDER)
	g++ -fno-rtti -fPIC -c -o $@ $(CWARNS) $(COPT) -I$(LUADIR)/include -I$(OMRDIR)/jitbuilder/release/include -I$(OMRDIR)/jitbuilder/release/include/compiler $<

omr:
	cd $(ROOTDIR) && git clone https://github.com/mstoodle/omr.git && \
	cd $(OMRDIR) && ./configure
	cd $(OMRDIR)/jitbuilder && make
