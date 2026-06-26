# configure/Makefile
#
# In labpva's direct-`mex` build there is nothing to compile or install from the
# configure directory itself (the optional *.local / RELEASE_SITE override files
# are read directly by configure/CONFIG). These standard phony targets are
# provided so the directory participates cleanly in top-level recursion and
# manual `make -C configure <target>` invocations.

TOP = ..
include $(TOP)/configure/CONFIG

.PHONY: all build install clean distclean realclean uninstall
all build install clean distclean realclean uninstall:
	@:
