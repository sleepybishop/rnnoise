OBJS=\
src/denoise.o\
src/rnn.o\
src/rnn_data.o\
src/rnn_reader.o\
src/pitch.o\
src/kiss_fft.o\
src/celt_lpc.o

CFLAGS+=-Iinclude -g -Wall --warn-vla
CFLAGS+= -O3 -std=c99 
#CFLAGS+=-march=native -mavx2  
#CFLAGS+=-fopt-info-vec-inline=vec.log
LDFLAGS+=-lm
#CC=clang

all: examples/rnnoise_demo examples/bertool

librnnoise.a: $(OBJS)
	$(AR) rcs $@ $^

src/denoise_training: CPPFLAGS+=-DTRAINING=1
src/denoise_training: clean $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

examples/rnnoise_demo: examples/rnnoise_demo.o librnnoise.a

.PHONY: clean indent scan

clean:
	$(RM) $(OBJS) examples/*.o *.a srcc/denoise_training examples/rnnoise_demo
	$(RM) gperf.svg

#indent:
#	clang-format -style=LLVM -i src/*.c examples/*.c include/*.h

scan:
	scan-build $(MAKE) clean all

gperf: LDFLAGS += -lprofiler -ltcmalloc
gperf: clean examples/rnnoise_demo examples/bertool
	CPUPROFILE_FREQUENCY=100000 CPUPROFILE=gperf.prof ./examples/rnnoise_demo sample.raw clean.raw
	pprof ./examples/rnnoise_demo gperf.prof --callgrind > callgrind.gperf
	gprof2dot --format=callgrind callgrind.gperf -z main | dot -T svg > gperf.svg
	examples/bertool clean.raw target.raw

valgrind:
	valgrind ./examples/rnnoise_demo sample_short.raw /dev/null

%.wav: %.raw
	sox -r 48k -e signed -b 16 -c 1 $^ $@
