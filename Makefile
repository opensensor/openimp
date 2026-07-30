PREFIX ?= /usr/local
PLATFORM ?= T31

.PHONY: all t31 t40 clean install

all:
	./build-for-device.sh $(PLATFORM)

t31:
	./build-t31.sh

t40:
	./build-t40.sh

clean:
	$(RM) -r build/t31 build/t40

install: all
	install -d "$(DESTDIR)$(PREFIX)/include/imp"
	install -d "$(DESTDIR)$(PREFIX)/lib"
	install -m 644 include/imp/*.h "$(DESTDIR)$(PREFIX)/include/imp/"
	install -m 755 "build/$(shell printf '%s' '$(PLATFORM)' | tr '[:upper:]' '[:lower:]')/libimp.so" \
		"$(DESTDIR)$(PREFIX)/lib/libimp.so"
