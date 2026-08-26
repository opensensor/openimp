PREFIX ?= /usr/local
PLATFORM ?= T31

.PHONY: all t23 t30 t31 t40 t41 check clean install

all:
	./build-for-device.sh $(PLATFORM)

t23:
	./build-t23.sh

t30:
	./build-t30.sh

t31:
	./build-t31.sh

t40:
	./build-t40.sh

t41:
	./build-t41.sh

check:
	$(MAKE) -C tests/t30 check
	$(MAKE) -C tests/t31 check
	$(MAKE) -C tests/t41 check

clean:
	$(RM) -r build/t23 build/t30 build/t31 build/t40 build/t41

install: all
	install -d "$(DESTDIR)$(PREFIX)/include/imp"
	install -d "$(DESTDIR)$(PREFIX)/include/openimp"
	install -d "$(DESTDIR)$(PREFIX)/bin"
	install -d "$(DESTDIR)$(PREFIX)/lib"
	install -m 644 include/imp/*.h "$(DESTDIR)$(PREFIX)/include/imp/"
	install -m 644 include/openimp/*.h "$(DESTDIR)$(PREFIX)/include/openimp/"
	install -m 755 "build/$(shell printf '%s' '$(PLATFORM)' | tr '[:upper:]' '[:lower:]')/libimp.so" \
		"$(DESTDIR)$(PREFIX)/lib/libimp.so"
	install -m 755 "build/$(shell printf '%s' '$(PLATFORM)' | tr '[:upper:]' '[:lower:]')/openimp-tuningd" \
		"$(DESTDIR)$(PREFIX)/bin/openimp-tuningd"
