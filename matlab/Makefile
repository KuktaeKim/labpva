# matlab/Makefile - build one MEX function per pva*.cpp.
#
# Each MEX is linked against the stateless glue objects ($(TOP)/glue/pvaConvert.o,
# $(TOP)/glue/mglue.o) plus the shared libmpvaglue.so (the stateful registry,
# built by glue/Makefile) and the EPICS pvAccess/pvData/normativeType/CA
# libraries. Output lands in $(TOP)/bin/$(EPICS_HOST_ARCH)/labpva, mirroring
# labca's bin/<arch>/labca layout; add that directory to your MATLAB path.
#
# Site paths / flags come from the EPICS-standard configure/ tree.

TOP = ..
include $(TOP)/configure/CONFIG

BINDIR = $(TOP)/bin/$(EPICS_HOST_ARCH)/labpva

# The stateful glue (pvaGlue.o, pvaError.o) lives in $(BINDIR)/libmpvaglue.so so
# all MEX share one copy of the monitor registry / provider / timeout / last-
# error state. Only the stateless mx* helpers are linked statically per-MEX.
GLUE_OBJS = $(TOP)/glue/pvaConvert.o $(TOP)/glue/mglue.o

# Resolve libmpvaglue.so at runtime from the MEX's own bin dir. Extend the
# CONFIG default rather than replace it (keep the EPICS_LIB rpath).
MEX_LDFLAGS = LDFLAGS='$$LDFLAGS -Wl,-rpath,$(EPICS_LIB) -Wl,-rpath,$(abspath $(BINDIR))'

# Every MEX entry point. (pvaPutShared.h / pvaMetaShared.h are headers, not
# entry points, so they are intentionally not listed.)
SRCS = \
	pvaGet.cpp pvaGetStructure.cpp \
	pvaPut.cpp pvaPutNoWait.cpp pvaPutStructure.cpp \
	pvaInfo.cpp \
	pvaSetMonitor.cpp pvaNewMonitorValue.cpp pvaNewMonitorWait.cpp pvaClear.cpp \
	pvaSetTimeout.cpp pvaGetTimeout.cpp pvaSetProvider.cpp pvaGetProvider.cpp \
	pvaDebugOn.cpp pvaDebugOff.cpp pvaLastError.cpp \
	pvaGetStatus.cpp pvaGetNelem.cpp \
	pvaGetControlLimits.cpp pvaGetGraphicLimits.cpp \
	pvaGetAlarmLimits.cpp pvaGetWarnLimits.cpp \
	pvaGetUnits.cpp pvaGetPrecision.cpp pvaGetEnumStrings.cpp

MEXFILES = $(patsubst %.cpp,$(BINDIR)/%.$(MEXEXT),$(SRCS))

.PHONY: all build install clean
all build install: $(MEXFILES)

$(BINDIR)/%.$(MEXEXT): %.cpp $(GLUE_OBJS)
	@mkdir -p $(BINDIR)
	$(MEX) $< $(GLUE_OBJS) -I. -I$(TOP)/glue $(EPICS_INCLUDES) \
		$(MEX_CXXFLAGS) \
		-L$(EPICS_LIB) $(PVA_LIBS) -L$(BINDIR) -lmpvaglue $(MEX_LDFLAGS) \
		-output $(BINDIR)/$*

clean:
	$(RM) $(MEXFILES)
