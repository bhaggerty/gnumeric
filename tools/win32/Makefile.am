export start module target

all: moduleset gnumeric.nsi

debug:: all
	$(abs_srcdir)/build $(abs_builddir) $(abs_srcdir) debug

release:: all
	$(abs_srcdir)/build $(abs_builddir) $(abs_srcdir) release

clean:
	rm -rf debug release

EXTRA_DIST = gnumeric.nsi.in gtkrc moduleset.in \
	build jhbuildrc.py

.PHONY : debug release all clean
