/* pvaGetStructure.cpp - read a whole pvAccess PVStructure as a MATLAB struct.
 *
 *   s = pvaGetStructure(pvname)
 *   s = pvaGetStructure(pvname, request)   request = pvRequest, default "field()"
 *   s = pvaGetStructure(pvname, poll)      poll (logical): force a fresh read
 *   s = pvaGetStructure(pvname, request, poll)
 *   c = pvaGetStructure({pv1,pv2,...})     1 x 1 struct each -> N x 1 cell
 *
 * This is the capability with no labca analogue: the entire structure tree
 * (value, alarm, timeStamp, display, control, and any custom/nested fields,
 * NTTable columns, NTNDArray dimensions, ...) is marshalled recursively into a
 * nested MATLAB struct. See ARCHITECTURE.md for the type-mapping table.
 *
 * Like pvaGet, this is served from the monitor cache BY DEFAULT: while a monitor
 * is active on the name (and its request covers the requested fields) the cached
 * value is returned with no network round-trip. Pass a trailing logical `poll`
 * = true to force a fresh server read (bypass the cache). With no active monitor
 * -- or when the monitor's request does not cover what was asked -- it reads
 * fresh regardless, so the result is always complete.
 */
#include "mglue.h"
#include "pvaGlue.h"
#include "pvaConvert.h"

using namespace labpva;

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    if (nrhs < 1)
        mexErrMsgIdAndTxt("labpva:invalidArg", "pvaGetStructure: need at least a PV name");

    PvaError err;
    bool wasCell = false;
    std::vector<std::string> pvs = buildPVs(prhs[0], wasCell, err);
    errCheck(err);

    /* Optional trailing args, in any order: a char pvRequest, and/or a logical
     * `poll` flag. Default request grabs the full structure; default behaviour
     * uses the monitor cache (poll == false). */
    std::string request = "field()";
    bool poll = false;
    for (int a = 1; a < nrhs; ++a) {
        const mxArray *m = prhs[a];
        if (mxIsChar(m))
            request = argString(m);
        else if (mxIsLogical(m) || mxIsNumeric(m))
            poll = (mxGetScalar(m) != 0);
    }
    if (request.empty()) request = "field()";

    std::vector<mxArray *> out;
    for (size_t i = 0; i < pvs.size(); ++i) {
        /* useMonitorCache defaults on (like pvaGet); requireWholeMonitor=true so
         * a narrow monitor never yields a partial tree. `poll` forces fresh. */
        PvValue pv = pvaGet(pvs[i], request, err,
                            /*useMonitorCache=*/!poll,
                            /*requireWholeMonitor=*/true);
        if (err.err != PVA_OK) break;
        out.push_back(pvStructureToMx(pv, err));
        if (err.err != PVA_OK) break;
    }
    errCheck(err);

    if (!wasCell) {
        plhs[0] = out.empty() ? mxCreateStructMatrix(1, 1, 0, NULL) : out[0];
    } else {
        mxArray *cell = mxCreateCellMatrix(out.size(), 1);
        for (size_t i = 0; i < out.size(); ++i) mxSetCell(cell, i, out[i]);
        plhs[0] = cell;
    }
}
