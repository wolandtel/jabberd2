INCLUDES = -DCONFIG_DIR=\"$(sysconfdir)\"

LIBTOOL += --quiet

bin_PROGRAMS = xmppd

noinst_HEADERS = xmppd.h
xmppd_SOURCES = main.c aci.c filter.c

xmppd_LDADD = $(top_builddir)/mio/libmio.la \
              $(top_builddir)/util/libutil.la
if USE_LIBSUBST
xmppd_LDADD += $(top_builddir)/subst/libsubst.la
endif
