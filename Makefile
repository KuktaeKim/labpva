# labpva top-level Makefile
#
#   make            build glue (.so) + MEX (.mexa64) + help stubs (.m)
#   make glue       build just the reusable glue objects + libmpvaglue.so
#   make matlab     build the MEX functions (builds glue first)
#   make stubs      (re)generate the per-verb .m help stubs (builds matlab first)
#   make clean      remove build products
#
# EPICS-standard configure/ layout (see configure/CONFIG): site paths come from
# configure/RELEASE, build knobs from configure/CONFIG_SITE, and arch/compiler
# settings from EPICS base. labpva keeps a direct-`mex` build, so the top-level
# recursion is explicit (glue before matlab) rather than the EPICS
# O.<arch>/RULES machinery.

TOP = .
include $(TOP)/configure/CONFIG

# Interpreter for the help-stub generator (doc/gen_help_stubs.py).
PYTHON ?= python3

DIRS = configure glue matlab
BINDIR = $(TOP)/bin/$(EPICS_HOST_ARCH)/labpva

.PHONY: all build install glue matlab stubs clean distclean realclean uninstall

all build install: stubs

glue:
	$(MAKE) -C glue

matlab: glue
	$(MAKE) -C matlab

# Help stubs: one same-named .m per verb (so `help pvaGet` works) + Contents.m,
# written into every bin/<arch>/labpva. Needs the MEX built first.
stubs: matlab
	$(PYTHON) $(TOP)/doc/gen_help_stubs.py

clean distclean realclean uninstall:
	$(MAKE) -C glue clean
	$(MAKE) -C matlab clean
	$(RM) -r $(BINDIR)
