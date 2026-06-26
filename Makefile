# labpva top-level Makefile
#
#   make            build the glue objects + shared lib, then all MEX functions
#   make glue       build just the reusable glue objects + libmpvaglue.so
#   make matlab     build the MEX functions (builds glue first)
#   make clean      remove build products
#
# EPICS-standard configure/ layout (see configure/CONFIG): site paths come from
# configure/RELEASE, build knobs from configure/CONFIG_SITE, and arch/compiler
# settings from EPICS base. labpva keeps a direct-`mex` build, so the top-level
# recursion is explicit (glue before matlab) rather than the EPICS
# O.<arch>/RULES machinery.

TOP = .
include $(TOP)/configure/CONFIG

DIRS = configure glue matlab

.PHONY: all build install glue matlab clean distclean realclean uninstall

all build install: matlab

glue:
	$(MAKE) -C glue

matlab: glue
	$(MAKE) -C matlab

clean distclean realclean uninstall:
	$(MAKE) -C glue clean
	$(MAKE) -C matlab clean
