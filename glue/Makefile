# glue/Makefile - compile the reusable glue objects and the shared glue library.
#
# These four objects (error handling, PVStructure<->mxArray marshalling,
# pvaClient lifecycle, MEX arg helpers) are compiled once with `mex -c` so they
# share MATLAB's exact ABI/flags. The two stateful, MATLAB-free objects are then
# linked into a shared libmpvaglue.so (see below); the other two are linked
# statically into every MEX by matlab/Makefile.
#
# Site paths / flags come from the EPICS-standard configure/ tree.

TOP = ..
include $(TOP)/configure/CONFIG

# Compiler used to LINK the shared glue library (this is not a MEX target). The
# glue .o are built -fPIC and reference no MATLAB symbols, so a plain g++ shared
# link is sufficient. Override CXX if your mex toolchain needs a matching g++.
CXX ?= g++

BINDIR = $(TOP)/bin/$(EPICS_HOST_ARCH)/labpva

OBJS = pvaError.o pvaConvert.o pvaGlue.o mglue.o

# The stateful, MATLAB-free objects -- the channel/monitor registry, the single
# PvaClient, and the provider/timeout/last-error globals -- are linked into ONE
# shared library that every MEX dynamically links against, so there is a single
# copy of that state in the MATLAB process (mirrors labca's libezca.so). Without
# this each MEX static-linked its own copy of pvaGlue.o, so a monitor set in
# pvaSetMonitor was invisible to pvaNewMonitorValue ("no monitor set"). The
# mx*-using objects (pvaConvert.o, mglue.o) are stateless helpers and stay
# static per-MEX.
SHLIB      = $(BINDIR)/libmpvaglue.so
SHLIB_OBJS = pvaGlue.o pvaError.o

.PHONY: all build install clean
all build install: $(OBJS) $(SHLIB)

# -I. so the glue headers find each other; EPICS includes for pvData/pvaClient.
%.o: %.cpp
	$(MEX) -c $< -I. $(EPICS_INCLUDES) $(MEX_CXXFLAGS) -outdir .

# Shared glue library. EPICS libs are linked in (with an rpath) so the .so is
# self-sufficient; each MEX then only needs -lmpvaglue.
$(SHLIB): $(SHLIB_OBJS)
	@mkdir -p $(BINDIR)
	$(CXX) -shared -o $@ $(SHLIB_OBJS) \
		-L$(EPICS_LIB) $(PVA_LIBS) -Wl,-rpath,$(EPICS_LIB)

pvaError.o:   pvaError.cpp   pvaError.h
pvaConvert.o: pvaConvert.cpp pvaConvert.h pvaError.h
pvaGlue.o:    pvaGlue.cpp    pvaGlue.h   pvaError.h
mglue.o:      mglue.cpp      mglue.h     pvaError.h

clean:
	$(RM) $(OBJS) $(SHLIB)
