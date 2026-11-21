# The autotest game linked with its symbols, for tools/bench/bench.sh:
#   stcmd make -f tools/bench/prof.mk st ATARI_MD_AUTOTEST=8 BUILDDIR=build/bench OBJDIR=obj/bench
# and its symbols as nm lists them, beside it.
ATARI_LDFLAGS = -nostdlib -L/freemint/libcmini/lib -m68000
include Makefile
st: $(BUILDDIR)/ROTT.nm
$(BUILDDIR)/ROTT.nm: $(ATARI_OUTPUT)
	m68k-atari-mint-nm -n $< > $@
