/* pvaIsMonitored.cpp - is a monitor currently active on a channel?
 *
 *   tf = pvaIsMonitored(pvname)              logical
 *   tf = pvaIsMonitored({pv1,pv2,...})       N x 1 logical column
 *
 * Read-only and NON-destructive: unlike pvaNewMonitorValue it does not consume
 * the new-value flag. True iff pvaSetMonitor is active on the name (i.e. set and
 * not yet pvaClear'd). No labca analogue.
 */
#include "mglue.h"
#include "pvaGlue.h"

using namespace labpva;

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    (void)nlhs;
    if (nrhs < 1)
        mexErrMsgIdAndTxt("labpva:invalidArg", "pvaIsMonitored: need a PV name");

    PvaError err;
    bool wasCell = false;
    std::vector<std::string> pvs = buildPVs(prhs[0], wasCell, err);
    errCheck(err);

    std::vector<char> flags;
    flags.reserve(pvs.size());
    for (size_t i = 0; i < pvs.size(); ++i)
        flags.push_back(pvaMonitorActive(pvs[i]) ? 1 : 0);

    if (!wasCell) {
        plhs[0] = mxCreateLogicalScalar(flags.empty() ? false : (flags[0] != 0));
    } else {
        mxArray *m = mxCreateLogicalMatrix(flags.size(), 1);
        mxLogical *p = mxGetLogicals(m);
        for (size_t i = 0; i < flags.size(); ++i) p[i] = flags[i] != 0;
        plhs[0] = m;
    }
}
