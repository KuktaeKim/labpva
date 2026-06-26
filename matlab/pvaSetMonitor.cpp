/* pvaSetMonitor.cpp - subscribe to value changes (cf. lcaSetMonitor).
 *
 *   pvaSetMonitor(pvname)
 *   pvaSetMonitor(pvname, request)            request = pvRequest
 *   pvaSetMonitor({pv1,pv2,...} [,request])
 *
 * After this, pvaNewMonitorValue / pvaNewMonitorWait report fresh samples and
 * pvaGet on the same channel is served from the monitor's cached value.
 */
#include "mglue.h"
#include "pvaGlue.h"

using namespace labpva;

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    (void)nlhs; (void)plhs;
    if (nrhs < 1)
        mexErrMsgIdAndTxt("labpva:invalidArg", "pvaSetMonitor: need at least a PV name");

    PvaError err;
    bool wasCell = false;
    std::vector<std::string> pvs = buildPVs(prhs[0], wasCell, err);
    errCheck(err);

    std::string request = (nrhs >= 2) ? argString(prhs[1]) : "field(value,alarm,timeStamp)";
    if (request.empty()) request = "field(value,alarm,timeStamp)";

    for (size_t i = 0; i < pvs.size(); ++i) {
        pvaMonitorSet(pvs[i], request, err);
        if (err.err != PVA_OK) break;
    }
    errCheck(err);
}
