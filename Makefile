
# Makefile, ECE252  
# Yiqing Huang 

CC = gcc
CFLAGS = -Wall -g -std=c99 
LD = gcc
LDFLAGS = -g -std=c99
LDLIBS = -pthread -lcurl -lz

LIB_UTIL = lab_png.o zutil.o crc.o 
SRCS   = lab_png.c crc.c zutil.c paster.c  

OBJS   = paster.o $(LIB_UTIL)

TARGETS= paster

all: ${TARGETS}

paster: $(OBJS) 
	$(LD) -o $@ $^ $(LDLIBS) $(LDFLAGS) 

%.o: %.c 
	$(CC) $(CFLAGS) -c $< 

%.d: %.c
	gcc -MM -MF $@ $<

-include $(SRCS:.c=.d)

.PHONY: clean
clean:
	rm -f *~ *.d *.o $(TARGETS) 
