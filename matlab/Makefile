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

# The stateful glue (pvaGlue_*.o, pvaError.o) lives in $(BINDIR)/libmpvaglue.so
# so all MEX share one copy of the monitor registry / provider / timeout / last-
# error state. Only the stateless mx* helpers are linked statically per-MEX.
# The marshalling object is backend-specific (LABPVA_BACKEND from configure/).
GLUE_OBJS = $(TOP)/glue/pvaConvert_$(LABPVA_BACKEND).o $(TOP)/glue/mglue.o

# Resolve libmpvaglue.so at runtime from the MEX's own bin dir, on top of the
# backend rpaths assembled in configure/CONFIG.
MEX_LDFLAGS = LDFLAGS='$$LDFLAGS $(PVA_RPATH) -Wl,-rpath,$(abspath $(BINDIR))'

# Every MEX entry point. (pvaPutShared.h / pvaMetaShared.h are headers, not
# entry points, so they are intentionally not listed.)
SRCS = \
	pvaGet.cpp pvaGetStructure.cpp \
	pvaPut.cpp pvaPutNoWait.cpp pvaPutStructure.cpp \
	pvaInfo.cpp \
	pvaSetMonitor.cpp pvaNewMonitorValue.cpp pvaNewMonitorWait.cpp pvaClear.cpp \
	pvaMonitors.cpp pvaIsMonitored.cpp pvaChannels.cpp pvaIsConnected.cpp \
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
	$(MEX) $< $(GLUE_OBJS) -I. -I$(TOP)/glue $(EPICS_INCLUDES) $(BACKEND_CPPFLAGS) \
		$(MEX_CXXFLAGS) \
		$(PVA_LIBDIRS) $(PVA_LIBS) -L$(BINDIR) -lmpvaglue $(MEX_LDFLAGS) \
		-output $(BINDIR)/$*

clean:
	$(RM) $(MEXFILES)
