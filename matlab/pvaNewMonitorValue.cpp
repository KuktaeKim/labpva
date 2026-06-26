/* pvaNewMonitorValue.cpp - has a monitored channel produced a new value?
 * (cf. lcaNewMonitorValue). Non-blocking.
 *
 *   tf = pvaNewMonitorValue(pvname)            logical
 *   tf = pvaNewMonitorValue({pv1,pv2,...})     N x 1 logical column
 *
 * Returns true once per arrived sample; the value itself is then read with
 * pvaGet (served from the monitor cache).
 */
#include "mglue.h"
#include "pvaGlue.h"

using namespace labpva;

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    (void)nlhs;
    if (nrhs < 1)
        mexErrMsgIdAndTxt("labpva:invalidArg", "pvaNewMonitorValue: need a PV name");

    PvaError err;
    bool wasCell = false;
    std::vector<std::string> pvs = buildPVs(prhs[0], wasCell, err);
    errCheck(err);

    std::vector<char> flags;
    flags.reserve(pvs.size());
    for (size_t i = 0; i < pvs.size(); ++i) {
        bool n = pvaMonitorPoll(pvs[i], err);
        if (err.err != PVA_OK) break;
        flags.push_back(n ? 1 : 0);
    }
    errCheck(err);

    if (!wasCell) {
        plhs[0] = mxCreateLogicalScalar(flags.empty() ? false : (flags[0] != 0));
    } else {
        mxArray *m = mxCreateLogicalMatrix(flags.size(), 1);
        mxLogical *p = mxGetLogicals(m);
        for (size_t i = 0; i < flags.size(); ++i) p[i] = flags[i] != 0;
        plhs[0] = m;
    }
}
