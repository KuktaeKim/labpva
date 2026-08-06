/* pvaGet.cpp - read the value of one or more pvAccess channels.
 *
 *   val          = pvaGet(pvname)
 *   val          = pvaGet(pvname, type)            type in 'NBSLFDC' (labca-style)
 *   val          = pvaGet(pvname, poll)            poll (logical): force fresh read
 *   val          = pvaGet(pvname, type, poll)
 *   [val, ts]    = pvaGet(...)                     ts = sec + i*nsec (complex)
 *   vals         = pvaGet({pv1,pv2,...} [,type] [,poll])   N x 1 column / cell
 *
 * For an NTScalar this returns the scalar in `.value` (drop-in for lcaGet);
 * for an NTScalarArray, the waveform; for an NTEnum, the selected choice
 * string (or its index if a numeric type is requested). For anything richer --
 * an NTNDArray image, an NTTable, a custom multi-field group, or any PV without
 * a top-level scalar/array `value` -- it returns the WHOLE structure as a nested
 * struct (the same tree pvaGetStructure gives). So pvaGet covers both cases:
 * bare value for scalars/arrays/enums, full structure otherwise.
 *
 * While a monitor is active on the name, the value is served from the monitor
 * cache (ezca-style, no network round-trip). Pass a trailing logical `poll`
 * = true to force a fresh server read instead.
 */
#include "mglue.h"
#include "pvaGlue.h"
#include "pvaConvert.h"

using namespace labpva;

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    if (nrhs < 1)
        mexErrMsgIdAndTxt("labpva:invalidArg", "pvaGet: need at least a PV name");

    PvaError err;
    bool wasCell = false;
    std::vector<std::string> pvs = buildPVs(prhs[0], wasCell, err);
    errCheck(err);

    /* Optional trailing args, in any order: a char type letter, and/or a logical
     * `poll` flag. Default uses the monitor cache (poll == false). */
    char type = 'N';
    bool poll = false;
    for (int a = 1; a < nrhs; ++a) {
        const mxArray *m = prhs[a];
        if (mxIsChar(m))
            type = parseTypeArg(m);
        else if ((mxIsLogical(m) || mxIsNumeric(m)) &&
                 mxGetNumberOfElements(m) >= 1)
            poll = (mxGetScalar(m) != 0);
    }
    bool wantTs = (nlhs >= 2);

    std::vector<mxArray *> vals;
    std::vector<double> secs, nsecs;
    for (size_t i = 0; i < pvs.size(); ++i) {
        /* Fetch the whole structure: a scalar/array/enum is still returned as a
         * bare value (pvValueToMx), but a rich PV (NTNDArray, NTTable, custom
         * group) comes back as the full nested struct -- so the result is
         * complete (e.g. an image's dimension/codec are present). */
        PvValue pv = pvaGet(pvs[i], "field()", err,
                            /*useMonitorCache=*/!poll);
        if (err.err != PVA_OK) break;                 /* defer error to scope exit */
        vals.push_back(pvValueToMx(pv, type, err));
        if (wantTs) {
            double s = 0, ns = 0;
            pvTimeStampSecNsec(pv, s, ns);
            secs.push_back(s); nsecs.push_back(ns);
        }
        if (err.err != PVA_OK) break;
    }
    errCheck(err);                                    /* PV ptrs already destroyed */

    plhs[0] = assembleValueOutput(vals, wasCell);
    if (wantTs) plhs[1] = complexColumn(secs, nsecs);
}
