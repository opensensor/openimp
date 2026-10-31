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

.PHONY: all t20 t21 t23 t30 t31 t40 t41 check export-check release-check clean install apitest

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
	$(MAKE) status-check
	$(MAKE) -f tests/core/su_base_shutdown.mk check
	$(MAKE) -f tests/core/su_sysutils.mk check
	$(MAKE) -C tests/eprc check
	$(MAKE) -C tests/rc_t20 check
	$(MAKE) -C tests/rc_t10 check
	$(MAKE) -C tests/t20 check
	$(MAKE) -C tests/t23 check
	$(MAKE) -C tests/t30 check
	$(MAKE) -C tests/t31 check
	$(MAKE) -C tests/t40 check
	$(MAKE) -C tests/t41 check
	$(MAKE) -C tests/testpattern check
	$(MAKE) -C tests/fallback check
	$(MAKE) -C tests/rc3_gate check
	$(MAKE) -C tests/device check

# Device test app (tools/apitest): make apitest PLATFORM=T31 TOOLCHAIN_PREFIX=<host/bin/mipsel-linux> APITEST_HEADERS=<ingenic-headers root>
# [DESTDIR=<dir>] builds build/<soc>/openimp-apitest against the libimp built before (T10: PLATFORM=T20 APITEST_SOC=T10) and, with
# DESTDIR set, installs it as $(DESTDIR)$(PREFIX)/bin/openimp-apitest.  See tools/apitest/README.md.
apitest:
	PLATFORM=$(PLATFORM) APITEST_SOC=$(APITEST_SOC) TOOLCHAIN_PREFIX=$(TOOLCHAIN_PREFIX) APITEST_HEADERS=$(APITEST_HEADERS) tools/apitest/build-device.sh
ifneq ($(DESTDIR),)
	install -D -m 755 "build/$(shell printf '%s' '$(PLATFORM)' | tr '[:upper:]' '[:lower:]')/openimp-apitest" "$(DESTDIR)$(PREFIX)/bin/openimp-apitest"
endif

# Built libimp.so exports vs vendor export lists vs streamer-used IMP_ names (tools/export-check/README.md)
# Status data (docs/status/*): schema validation + FEATURE_MATRIX.md is what the data produces (needs PyYAML)
status-check:
	python3 tools/status_data.py --check
	python3 tools/gen_feature_matrix_html.py --check
	python3 tools/apitest/check_coverage.py
	python3 tools/gen_api_wiki.py --check

# Host only (needs the soak sampler logs and the camera STATUS files, not CI): daily soak summary, then regenerate the matrix
status-soak:
	python3 tools/status_soak.py
	python3 tools/gen_feature_matrix_html.py

.PHONY: status-check status-soak api-wiki

# API reference pages of the wiki (ISP, FrameSource, Encoder) from include/, docs/api_evidence.json and docs/api_reference/*.yaml
# into a wiki checkout: make api-wiki WIKI_DIR=<openimp.wiki checkout>   (needs PyYAML; tools/gen_api_wiki.py)
api-wiki:
	@test -n "$(WIKI_DIR)" || { echo "usage: make api-wiki WIKI_DIR=<wiki checkout dir>"; exit 2; }
	python3 tools/gen_api_wiki.py --out "$(WIKI_DIR)"

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
