PREFIX ?= /usr/local
PLATFORM ?= T31
# T20/T21/T23/T30: OPENIMP_SW_JPEG=1 (make t21 OPENIMP_SW_JPEG=1) builds the
# software JPEG fallback next to the Helix hardware encoder; default 0.
ifdef OPENIMP_SW_JPEG
export OPENIMP_SW_JPEG
endif
# OPENIMP_DEBUG=1 (make t31 OPENIMP_DEBUG=1) keeps LOG_DEBUG and the developer
# traces in the build; default 0 compiles them out (smaller libimp.so).
ifdef OPENIMP_DEBUG
export OPENIMP_DEBUG
endif
# OPENIMP_SIZE_OPT=0 (make t31 OPENIMP_SIZE_OPT=0) builds everything at -O2;
# default 1 builds at -Os and keeps -O2 for the hot paths (docs/SIZE.md).
ifdef OPENIMP_SIZE_OPT
export OPENIMP_SIZE_OPT
endif
# OPENIMP_T23_HELIX_BRIDGE=1 (make t23 OPENIMP_T23_HELIX_BRIDGE=1) builds the
# OEM Helix worker bridge into the T23 libimp (default 0, docs/SIZE.md).
ifdef OPENIMP_T23_HELIX_BRIDGE
export OPENIMP_T23_HELIX_BRIDGE
endif

.PHONY: all t20 t21 t23 t30 t31 t40 t41 check export-check release-check clean install

all:
	./build-for-device.sh $(PLATFORM)

t20:
	./build-t20.sh

t21:
	./build-t21.sh

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
	$(MAKE) -f tests/core/su_base_shutdown.mk check
	$(MAKE) -C tests/eprc check
	$(MAKE) -C tests/rc_t20 check
	$(MAKE) -C tests/rc_t10 check
	$(MAKE) -C tests/t20 check
	$(MAKE) -C tests/t23 check
	$(MAKE) -C tests/t30 check
	$(MAKE) -C tests/t31 check
	$(MAKE) -C tests/t40 check
	$(MAKE) -C tests/t41 check
	$(MAKE) -C tests/device check

# Built libimp.so exports vs vendor export lists vs streamer-used IMP_ names (tools/export-check/README.md)
export-check:
	./tools/export-check.sh

# Release gate (slow): make check, build every SoC, export-check in strict mode (tools/release-check.sh)
release-check:
	./tools/release-check.sh

clean:
	$(RM) -r build/t20 build/t21 build/t23 build/t30 build/t31 build/t40 build/t41

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
