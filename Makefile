TARGET = osmrail
all: $(TARGET)

CC = gcc
CFLAGS = -pthread -O3 -Wall
LDFLAGS = 

INSTALL = /usr/bin/install -c
prefix = /usr/local
exec_prefix = ${prefix}
BINDIR = ${exec_prefix}/bin

INCLUDES = 
LIBS = -lbz2

OBJS = osmrail.o osm_planet.o osm_parse.o
DEPS = osm.h

%.o: %.c $(DEPS)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $^ $(LIBS)

install: $(TARGET)
	-mkdir -p $(BINDIR)
	$(INSTALL) $(TARGET) $(BINDIR)

clean:
	rm -f $(OBJS) $(TARGET)
