/* pvaIsConnected.cpp - is a channel currently connected to its IOC?
 *
 *   tf = pvaIsConnected(pvname)              logical
 *   tf = pvaIsConnected({pv1,pv2,...})       N x 1 logical column
 *
 * Reports the LIVE connection state of a channel labpva has already opened.
 * Returns false if labpva never opened the channel -- it does NOT open a new
 * one just to check (no side effect / no accidental connect). No labca analogue.
 */
#include "mglue.h"
#include "pvaGlue.h"

using namespace labpva;

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    (void)nlhs;
    if (nrhs < 1)
        mexErrMsgIdAndTxt("labpva:invalidArg", "pvaIsConnected: need a PV name");

    PvaError err;
    bool wasCell = false;
    std::vector<std::string> pvs = buildPVs(prhs[0], wasCell, err);
    errCheck(err);

    std::vector<char> flags;
    flags.reserve(pvs.size());
    for (size_t i = 0; i < pvs.size(); ++i)
        flags.push_back(pvaChannelConnected(pvs[i]) ? 1 : 0);

    if (!wasCell) {
        plhs[0] = mxCreateLogicalScalar(flags.empty() ? false : (flags[0] != 0));
    } else {
        mxArray *m = mxCreateLogicalMatrix(flags.size(), 1);
        mxLogical *p = mxGetLogicals(m);
        for (size_t i = 0; i < flags.size(); ++i) p[i] = flags[i] != 0;
        plhs[0] = m;
    }
}
