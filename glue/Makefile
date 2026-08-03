# glue/Makefile - compile the reusable glue objects and the shared glue library.
#
# Four objects (error handling, structure<->mxArray marshalling, client
# lifecycle, MEX arg helpers) are compiled once with `mex -c` so they share
# MATLAB's exact ABI/flags. The two stateful, MATLAB-free objects are then
# linked into a shared libmpvaglue.so (see below); the other two are linked
# statically into every MEX by matlab/Makefile.
#
# The client backend is selected in configure/ (LABPVA_BACKEND = pvac | pvxs,
# driven by whether PVXS is set in configure/RELEASE): the backend-specific
# sources are pvaGlue_$(LABPVA_BACKEND).cpp and pvaConvert_$(LABPVA_BACKEND).cpp.
#
# Site paths / flags come from the EPICS-standard configure/ tree.

TOP = ..
include $(TOP)/configure/CONFIG

# Compiler used to LINK the shared glue library (this is not a MEX target). The
# glue .o are built -fPIC and reference no MATLAB symbols, so a plain g++ shared
# link is sufficient. Override CXX if your mex toolchain needs a matching g++.
CXX ?= g++

BINDIR = $(TOP)/bin/$(EPICS_HOST_ARCH)/labpva

GLUE_O    = pvaGlue_$(LABPVA_BACKEND).o
CONVERT_O = pvaConvert_$(LABPVA_BACKEND).o
OBJS      = pvaError.o $(CONVERT_O) $(GLUE_O) mglue.o

# The stateful, MATLAB-free objects -- the channel/monitor registry, the single
# client, and the provider/timeout/last-error globals -- are linked into ONE
# shared library that every MEX dynamically links against, so there is a single
# copy of that state in the MATLAB process (mirrors labca's libezca.so). Without
# this each MEX static-linked its own copy of the glue, so a monitor set in
# pvaSetMonitor was invisible to pvaNewMonitorValue ("no monitor set"). The
# mx*-using objects (pvaConvert_*.o, mglue.o) are stateless helpers and stay
# static per-MEX.
SHLIB      = $(BINDIR)/libmpvaglue.so
SHLIB_OBJS = $(GLUE_O) pvaError.o

.PHONY: all build install clean
all build install: $(OBJS) $(SHLIB)

# -I. so the glue headers find each other; EPICS includes cover the backend's
# headers too (configure/CONFIG adds -I$(PVXS)/include when PVXS is set).
%.o: %.cpp
	$(MEX) -c $< -I. $(EPICS_INCLUDES) $(BACKEND_CPPFLAGS) $(MEX_CXXFLAGS) -outdir .

# Shared glue library. The backend's client libs are linked in (with rpaths)
# so the .so is self-sufficient; each MEX then only needs -lmpvaglue.
$(SHLIB): $(SHLIB_OBJS)
	@mkdir -p $(BINDIR)
	$(CXX) -shared -o $@ $(SHLIB_OBJS) \
		$(PVA_LIBDIRS) $(PVA_LIBS) $(PVA_RPATH)

pvaError.o:            pvaError.cpp           pvaError.h
pvaConvert_pvac.o:     pvaConvert_pvac.cpp    pvaConvert.h pvaBackend.h pvaError.h
pvaConvert_pvxs.o:     pvaConvert_pvxs.cpp    pvaConvert.h pvaBackend.h pvaError.h
pvaGlue_pvac.o:        pvaGlue_pvac.cpp       pvaGlue.h    pvaBackend.h pvaError.h
pvaGlue_pvxs.o:        pvaGlue_pvxs.cpp       pvaGlue.h    pvaBackend.h pvaError.h
mglue.o:               mglue.cpp              mglue.h      pvaError.h

clean:
	$(RM) pvaError.o pvaConvert_*.o pvaGlue_*.o mglue.o $(SHLIB)
