/* pvaNewMonitorWait.cpp - block until a monitored channel produces a new
 * value (cf. lcaNewMonitorWait).
 *
 *   pvaNewMonitorWait(pvname)
 *   pvaNewMonitorWait(pvname, timeout)         seconds (0 => configured default)
 *   tf = pvaNewMonitorWait({pv1,...} [,timeout])   N x 1 logical (false=timed out)
 *
 * For a list, each channel is waited on in turn.
 */
#include "mglue.h"
#include "pvaGlue.h"

using namespace labpva;

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    (void)nlhs;
    if (nrhs < 1)
        mexErrMsgIdAndTxt("labpva:invalidArg", "pvaNewMonitorWait: need a PV name");

    PvaError err;
    bool wasCell = false;
    std::vector<std::string> pvs = buildPVs(prhs[0], wasCell, err);
    errCheck(err);

    double timeout = 0.0;
    if (nrhs >= 2 && mxIsNumeric(prhs[1]) && mxGetNumberOfElements(prhs[1]) >= 1)
        timeout = mxGetScalar(prhs[1]);

    std::vector<char> flags;
    flags.reserve(pvs.size());
    for (size_t i = 0; i < pvs.size(); ++i) {
        bool got = pvaMonitorWait(pvs[i], timeout, err);
        if (err.err != PVA_OK) break;
        flags.push_back(got ? 1 : 0);
    }
    errCheck(err);

    if (nlhs >= 1) {
        if (!wasCell) {
            plhs[0] = mxCreateLogicalScalar(flags.empty() ? false : (flags[0] != 0));
        } else {
            mxArray *m = mxCreateLogicalMatrix(flags.size(), 1);
            mxLogical *p = mxGetLogicals(m);
            for (size_t i = 0; i < flags.size(); ++i) p[i] = flags[i] != 0;
            plhs[0] = m;
        }
    }
}
