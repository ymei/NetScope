OSTYPE = $(shell uname)
ARCH   = $(shell uname -m)
##################################### Defaults ################################
CC             := gcc
INCLUDE        := -I.
CFLAGS         := -Wall -O2
CFLAGS_32      := -m32
SHLIB_CFLAGS   := -fPIC -shared
SHLIB_EXT      := .so
LIBS           := -lm
LDFLAGS        :=
############################# Library add-ons #################################
INCLUDE += -I/opt/local/include -I/usr/local/include
LIBS    += -L/opt/local/lib -L/usr/local/lib -lpthread -lhdf5
GLLIBS   =
############################# OS & ARCH specifics #############################
ifneq ($(OSTYPE), Linux)
  ifeq ($(OSTYPE), Darwin)
    CC            = clang
    GLLIBS       += -framework GLUT -framework OpenGL -framework Cocoa
    SHLIB_CFLAGS := -dynamiclib
    SHLIB_EXT    := .dylib
    ifeq ($(shell sysctl -n hw.optional.x86_64), 1)
      ARCH       := x86_64
    endif
  else ifeq ($(OSTYPE), FreeBSD)
    CC      = clang
    GLLIBS += -lGL -lGLU -lglut
  else ifeq ($(OSTYPE), SunOS)
    CFLAGS     := -Wall
  else
    # Let's assume this is win32
    SHLIB_EXT  := .dll
  endif
else
  GLLIBS += -lGL -lGLU -lglut
endif

ifneq ($(ARCH), x86_64)
  CFLAGS_32 += -m32
endif

# Are all G5s ppc970s?
ifeq ($(ARCH), ppc970)
  CFLAGS += -m64
endif
############################ Define targets ###################################
EXE_TARGETS = dpo5054 wavedump
DEBUG_EXE_TARGETS = hdf5io
# SHLIB_TARGETS = XXX$(SHLIB_EXT)

ifeq ($(ARCH), x86_64) # compile a 32bit version on 64bit platforms
  # SHLIB_TARGETS += XXX_m32$(SHLIB_EXT)
endif

.PHONY: exe_targets shlib_targets debug_exe_targets clean
exe_targets: $(EXE_TARGETS)
shlib_targets: $(SHLIB_TARGETS)
debug_exe_targets: $(DEBUG_EXE_TARGETS)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDE) -c $< -o $@
dpo5054: main.c hdf5io.o fifo.o
	$(CC) $(CFLAGS) $(INCLUDE) $^ $(LIBS) $(LDFLAGS) -o $@
analyze_pe: analysis/analyze_pe.c hdf5io.o
	$(CC) $(CFLAGS) $(INCLUDE) $^ $(LIBS) $(LDFLAGS) -o $@
analyze_int: analysis/analyze_int.c hdf5io.o
	$(CC) $(CFLAGS) $(INCLUDE) $^ $(LIBS) $(LDFLAGS) -o $@
wavedump: analysis/wavedump.c hdf5io.o
	$(CC) $(CFLAGS) $(INCLUDE) $^ $(LIBS) $(LDFLAGS) -o $@
hdf5io.o: hdf5io.c hdf5io.h
	$(CC) $(CFLAGS) -DH5_NO_DEPRECATED_SYMBOLS $(INCLUDE) -c $<
hdf5io: hdf5io.c hdf5io.h
	$(CC) $(CFLAGS) -DH5_NO_DEPRECATED_SYMBOLS $(INCLUDE) -DHDF5IO_DEBUG_ENABLEMAIN $< $(LIBS) $(LDFLAGS) -o $@
fifo.o: fifo.c fifo.h
	$(CC) $(CFLAGS) $(INCLUDE) -c $<
fifo_test: fifo.c fifo.h
	$(CC) $(CFLAGS) $(INCLUDE) -DFIFO_DEBUG_ENABLEMAIN $< $(LIBS) $(LDFLAGS) -o $@
clean:
	rm -f *.o
