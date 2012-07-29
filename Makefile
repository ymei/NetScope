CC=clang
CFLAGS=-Wall -std=c99 -O2
INCLUDE=-I/opt/local/include -I./fifo
LIBS=-L/opt/local/lib -lpthread -lhdf5

.PHONY: all clean
all: dpo5054 wavedump
dpo5054: main.c hdf5io.o
	$(CC) $(CFLAGS) $(INCLUDE) $^ $(LIBS) $(LDFLAGS) -o $@
analyze_spe: analysis/analyze_spe.c hdf5io.o
	$(CC) $(CFLAGS) $(INCLUDE) $^ $(LIBS) $(LDFLAGS) -o $@
analyze_int: analysis/analyze_int.c hdf5io.o
	$(CC) $(CFLAGS) $(INCLUDE) $^ $(LIBS) $(LDFLAGS) -o $@
wavedump: analysis/wavedump.c hdf5io.o
	$(CC) $(CFLAGS) $(INCLUDE) $^ $(LIBS) $(LDFLAGS) -o $@
hdf5io.o: hdf5io.c hdf5io.h
	$(CC) $(CFLAGS) -DH5_NO_DEPRECATED_SYMBOLS $(INCLUDE) -c $<
hdf5io: hdf5io.c hdf5io.h
	$(CC) $(CFLAGS) -DH5_NO_DEPRECATED_SYMBOLS $(INCLUDE) -DHDF5IO_DEBUG_ENABLEMAIN $< $(LIBS) $(LDFLAGS) -o $@
pipe.o: fifo/pipe.c fifo/pipe.h
	$(CC) $(CFLAGS) $(INCLUDE) -c $<
clean:
	rm -f *.o
