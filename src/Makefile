include ../Makefile.config

.PHONY: all
all: chmod-default Conver CppToHTML Judge_machine server lib setup-install

.PHONY: Conver CppToHTML Judge_machine server lib
Conver CppToHTML Judge_machine server: lib
Conver CppToHTML Judge_machine server lib:
	$(Q)$(MAKE) -C $@/

chmod-default: Algorithms/other/chmod-default.cpp
	$(Q)$(call P,LINK,$@)$(CXX) $^ $(CXXFLAGS) -o $@

db_init_dump.c: db_init.sql
	$(Q)$(call P,GEN,$@)xxd -i $< > $@

setup-install: setup_install.o db_init_dump.o lib/libsim.a
	$(Q)$(call P,LINK,$@)$(CXX) $^ $(CXXFLAGS) -o $@ -lsqlite3

lib/libsim.a: lib
	@

.PHONY: clean
clean:
	$(Q)$(RM) *.o *.deps chmod-default sim-server db_init_dump.c setup-install
	$(Q)$(MAKE) clean -C lib/
	$(Q)$(MAKE) clean -C Conver/
	$(Q)$(MAKE) clean -C CppToHTML/
	$(Q)$(MAKE) clean -C Judge_machine/
	$(Q)$(MAKE) clean -C server/
