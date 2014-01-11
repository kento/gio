
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
	-srun rm $(TARGET_DIR)/gio.* 2>&1 /dev/null
#	srun  -n 4 ./$(gio_PROGRAM) -e sw -s w -f 1048576 -d /tmp
	srun  -n 4 ./$(gio_PROGRAM) -e pw -s w -f 1048576 -d $(TARGET_DIR)

$(gio_PROGRAM): $(gio_OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

.c.o: 
	$(CC) $(CFLAGS) $(LDFLAGS) -c $<

.PHONY: clean
clean:
	-rm -rf $(PROGRAMS) $(OBJS)