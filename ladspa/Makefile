OBJS=denoise.o\
mmapring.o

CFLAGS=-O2 -shared -g -fPIC -I. -I../include/ 
LDFLAGS=-lm

all: librnnoise_ladspa.so

librnnoise_ladspa.so: $(OBJS)
	$(CC) $(CFLAGS) $^ $(LDFLAGS) ../librnnoise.a -o $@

.PHONY: clean
clean:
	$(RM) $(OBJS) 
