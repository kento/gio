
gio_OBJS = gio.o gio_err.o gio_io.o gio_mem.o gio_util.o
gio_PROGRAM = gio

PROGRAMS= $(gio_PROGRAM)
OBJS= $(gio_OBJS)

CC = mpicc
LDFLAGS = -I/usr/include/ -L/usr/lib64/
CFLAGS = -Wall -O2

.SUFFIXES: .c .o

all: $(gio_PROGRAM) 

TARGET_DIR=.

test: $(gio_PROGRAM)	
#	-srun rm $(TARGET_DIR)/gio-file.* 2> /tmp/null 1> /tmp/null
#	srun  -n 4 ./$(gio_PROGRAM) -e sw -s w -f 1048576 -d /tmp
	srun  -n 4 ./$(gio_PROGRAM) -e pw -s w -f 1048576 -d $(TARGET_DIR) -m 4
	srun  -n 4 ./$(gio_PROGRAM) -e pr -s w -f 1048576 -d $(TARGET_DIR) -m 4

$(gio_PROGRAM): $(gio_OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

.c.o: 
	$(CC) $(CFLAGS) $(LDFLAGS) -c $<

.PHONY: clean
clean-all: clean gclean

clean:
	-rm -rf $(PROGRAMS) $(OBJS)

gclean:
	-rm -rf ./*.core

dclean:
	-rm -rf ./gio-file.*