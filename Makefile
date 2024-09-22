TOOLS	 = conntrack-flush route-monitor conntrack-nat-callidus
TOOLS	+= route-show
SERVICES = udhcpc-monitor

all: $(TOOLS) $(SERVICES)

clean:
	rm -f *.o $(TOOLS) $(SERVICES)

PREFIX ?= /usr/local

install: $(TOOLS) $(SERVICES)
	install -D -d $(DESTDIR)/$(PREFIX)/bin
	install -s -m 0755 $(TOOLS) $(DESTDIR)/$(PREFIX)/bin
	install -D -d $(DESTDIR)/$(PREFIX)/sbin
	install -s -m 0755 $(SERVICES) $(DESTDIR)/$(PREFIX)/sbin
	install -D -d $(DESTDIR)/etc/init.d
	install -m 0755 udhcpc-monitor.init $(DESTDIR)/etc/init.d/udhcpc-monitor

NL_DEPS = "libnl-3.0 libnl-route-3.0"
CONNTRACK_DEPS = "libnetfilter_conntrack"

conntrack-flush: CFLAGS += `pkg-config $(CONNTRACK_DEPS) --cflags`
conntrack-flush: LDLIBS += `pkg-config $(CONNTRACK_DEPS) --libs`
conntrack-flush: nfct-flush-net.o

route-monitor: CFLAGS += `pkg-config $(NL_DEPS) --cflags`
route-monitor: LDLIBS += `pkg-config $(NL_DEPS) --libs`
route-monitor: nl-monitor.o rt-label.o

route-show: CFLAGS += `pkg-config $(NL_DEPS) --cflags`
route-show: LDLIBS += `pkg-config $(NL_DEPS) --libs`
route-show: nl-monitor.o rt-label.o

udhcpc-monitor: CFLAGS += `pkg-config $(NL_DEPS) --cflags`
udhcpc-monitor: LDLIBS += `pkg-config $(NL_DEPS) --libs`
udhcpc-monitor: nl-monitor.o

conntrack-nat-callidus: CFLAGS += `pkg-config $(NL_DEPS) --cflags`
conntrack-nat-callidus: LDLIBS += `pkg-config $(NL_DEPS) --libs`
conntrack-nat-callidus: CFLAGS += `pkg-config $(CONNTRACK_DEPS) --cflags`
conntrack-nat-callidus: LDLIBS += `pkg-config $(CONNTRACK_DEPS) --libs`
conntrack-nat-callidus: nl-monitor.o nfct-flush-net.o
