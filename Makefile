TARGETS = conntrack-flush route-monitor conntrack-nat-callidus

all: $(TARGETS)

clean:
	rm -f *.o $(TARGETS)

PREFIX ?= /usr/local

install: $(TARGETS)
	install -D -d $(DESTDIR)/$(PREFIX)/bin
	install -s -m 0755 $^ $(DESTDIR)/$(PREFIX)/bin

NL_DEPS = "libnl-1"
CONNTRACK_DEPS = "libnetfilter_conntrack"

conntrack-flush: CFLAGS += `pkg-config $(CONNTRACK_DEPS) --cflags --libs`
conntrack-flush: nfct-flush-net.o

route-monitor: CFLAGS += `pkg-config $(NL_DEPS) --cflags --libs`
route-monitor: nl-monitor.o rt-label.o

conntrack-nat-callidus: CFLAGS += `pkg-config $(NL_DEPS) --cflags --libs`
conntrack-nat-callidus: CFLAGS += `pkg-config $(CONNTRACK_DEPS) --cflags --libs`
conntrack-nat-callidus: nl-monitor.o nfct-flush-net.o
