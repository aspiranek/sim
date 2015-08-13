include Makefile.config

DESTDIR = build

.PHONY: all
all:
	@printf "CC -> $(CC)\nCXX -> $(CXX)\n"
	$(Q)$(MAKE) -C src/
	@printf "\033[;32mBuild finished\033[0m\n"

.PHONY: install
install: all
	# Echo log
	@printf "\033[01;34m$(abspath $(DESTDIR))\033[0m\n"

	# Installation
	$(MKDIR) $(abspath $(DESTDIR)/problems/)
	$(MKDIR) $(abspath $(DESTDIR)/solutions/)
	$(MKDIR) $(abspath $(DESTDIR)/submissions/)
	$(MKDIR) $(abspath $(DESTDIR)/public/)
	$(UPDATE) src/public src/sim-server src/conver src/judge-machine src/CTH $(abspath $(DESTDIR))

	# Install PRoot
ifeq ($(shell uname -m), x86_64)
	$(UPDATE) bin/proot-x86_64 $(abspath $(DESTDIR)/proot)
else
	$(UPDATE) bin/proot-x86 $(abspath $(DESTDIR)/proot)
endif

	# Set up install
	- src/setup-install $(abspath $(DESTDIR))

	# Set owner, group and permission bits
	src/chmod-default $(abspath $(DESTDIR))
	chmod 0700 $(abspath $(DESTDIR)/.sim.db) $(abspath $(DESTDIR)/solutions) $(abspath $(DESTDIR)/problems)
	chmod +x $(abspath $(DESTDIR)/sim-server) $(abspath $(DESTDIR)/conver) $(abspath $(DESTDIR)/judge-machine) $(abspath $(DESTDIR)/CTH) $(abspath $(DESTDIR)/proot)

	@printf "\033[;32mInstallation finished\033[0m\n"

.PHONY: reinstall
reinstall:
	$(RM) $(abspath $(DESTDIR)/.db.config)
	$(MAKE) install

.PHONY: run
run:
	cd $(DESTDIR) && ./judge-machine&
	cd $(DESTDIR) && ./sim-server&

.PHONY: clean
clean:
	$(Q)$(MAKE) clean -C src/

.PHONY: help
help:
	@echo "Nothing is here yet..."
