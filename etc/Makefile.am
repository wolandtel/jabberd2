LIBTOOL += --quiet

configdir = $(sysconfdir)/$(PACKAGE)
config_DATA = c2s.xml.dist router.xml.dist s2s.xml.dist sm.xml.dist $(PACKAGE).cfg.dist router-users.xml.dist router-filter.xml.dist
sysvinitdir = $(sysconfdir)/init.d
sysvinit_DATA = $(PACKAGE)
initdir = $(sysconfdir)/init
init_DATA = $(PACKAGE)-c2s.conf $(PACKAGE)-router.conf $(PACKAGE)-s2s.conf $(PACKAGE)-sm.conf
systemddir = $(prefix)/lib/systemd/system
systemd_DATA = $(PACKAGE)-sm.service $(PACKAGE).service $(PACKAGE)-s2s.service $(PACKAGE)-router.service $(PACKAGE)-c2s.service
configs = $(config_DATA) $(sysvinit_DATA) $(init_DATA) $(systemd_DATA)
EXTRA_DIST = $(config_DATA:%.dist=%.dist.in) $(init_DATA:%.conf=%.conf.in) $(systemd_DATA:%.service=%.service.in)

SUBDIRS = templates

package_bin = router sm s2s c2s

edit = sed \
	-e 's,@package\@,$(PACKAGE),g' \
	-e 's,@sysconfdir\@,$(sysconfdir),g' \
	-e 's,@localstatedir\@,$(localstatedir),g' \
	-e 's,@bindir\@,$(bindir),g' \
	-e 's,@libdir\@,$(libdir),g' \
	-e 's,@pkglibdir\@,$(pkglibdir),g' \
	-e 's,@sysvinitdir\@,$(sysvinitdir),g' \
	-e 's,@configdir\@,$(configdir),g'

$(configs): $(config_DATA:%.dist=@srcdir@/%.dist.in) $(init_DATA:%.conf=@srcdir@/%.conf.in) $(systemd_DATA:%.service=%.service.in)
	@echo "generating $@ from $@.in"; \
    edit='$(edit)'; \
    list='$(package_bin)'; for p in $$list; do \
      bin=`echo "$$p" | sed 's,^.*/,,;$(transform);s/$$/$(EXEEXT)/'`; \
      edit="$$edit -e s,@package_$$p\_bin\\@,$$bin,g"; \
    done; \
	rm -f $@ $@.tmp; \
	eval "$$edit < @srcdir@/$@.in > $@.tmp"; \
	mv $@.tmp $@ 
    
install-data-hook:
	@list='$(config_DATA)'; for p in $$list; do \
      dest=`echo $$p | sed -e s/.dist//`; \
	  if test -f $(DESTDIR)$(configdir)/$$dest; then \
        echo "$@ will not overwrite existing $(DESTDIR)$(configdir)/$$dest"; \
      else \
	    echo " $(INSTALL_DATA) $$p $(DESTDIR)$(configdir)/$$dest"; \
	    $(INSTALL_DATA) $$p $(DESTDIR)$(configdir)/$$dest; \
	  fi; \
	done

clean-local:
	rm -f $(configs)
