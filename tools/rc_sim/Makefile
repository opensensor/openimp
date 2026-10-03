# Builds librcsim.so from the vendor-identical controller ports, taken
# unchanged from their branches (override the refs to test other commits).
EPRC_REF ?= origin/claude/eprc-t21-t23
T20_REF  ?= origin/claude/t20-rc
CC ?= cc
CFLAGS ?= -std=gnu99 -O2 -fPIC -ffp-contract=off -w
B := build

FILES := $(B)/eprc/eprc.c $(B)/eprc/eprc.h $(B)/eprc/eprc_internal.h \
	$(B)/rc_t20/rc_t20.c $(B)/rc_t20/rc_t20.h $(B)/rc_t20/rc_t20_internal.h \
	$(B)/rc_t20/rc_t20_mb.c $(B)/rc_t10/rc_t10.c $(B)/rc_t10/rc_t10.h

all: $(B)/librcsim.so

$(B)/eprc/%: ; @mkdir -p $(dir $@); git show $(EPRC_REF):src/eprc/$* > $@
# The only change to the ports: one study hook each at the per-picture
# nominal budget of the P-picture QP (rcsim_hook_per() returns its argument
# unless a variant enables it), see rcsim.c.
HOOK_DECL := int rcsim_hook_per(int per);
$(B)/rc_t20/rc_t20.c:
	@mkdir -p $(dir $@)
	git show $(T20_REF):src/rc_t20/rc_t20.c | sed -e '1i $(HOOK_DECL)' \
		-e 's/^    tgt = RI32(S, 128);$$/    tgt = rcsim_hook_per(RI32(S, 128));/' > $@
	grep -q rcsim_hook_per\(RI32 $@
$(B)/rc_t10/rc_t10.c:
	@mkdir -p $(dir $@)
	git show $(T20_REF):src/rc_t10/rc_t10.c | sed -e '1i $(HOOK_DECL)' \
		-e 's/^    avg = RI32(S, 116);$$/    avg = rcsim_hook_per(RI32(S, 116));/' > $@
	grep -q rcsim_hook_per\(RI32 $@
$(B)/rc_t20/%: ; @mkdir -p $(dir $@); git show $(T20_REF):src/rc_t20/$* > $@
$(B)/rc_t10/%: ; @mkdir -p $(dir $@); git show $(T20_REF):src/rc_t10/$* > $@

$(B)/librcsim.so: rcsim.c $(FILES)
	$(CC) $(CFLAGS) -shared -I$(B) -o $@ rcsim.c $(B)/eprc/eprc.c \
		$(B)/rc_t20/rc_t20.c $(B)/rc_t20/rc_t20_mb.c $(B)/rc_t10/rc_t10.c -lm

clean:
	rm -rf $(B)
.PHONY: all clean
