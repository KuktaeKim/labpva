/* pvaGetPrecision.cpp - display precision (cf. lcaGetPrecision).
 *   p = pvaGetPrecision(pvname[s])     from the NT `display.precision` field
 */
#include "pvaMetaShared.h"
using namespace labpva;
void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    (void)nlhs;
    if (nrhs < 1)
        mexErrMsgIdAndTxt("labpva:invalidArg", "pvaGetPrecision: need a PV name");
    PvaError err;
    bool wasCell = false;
    std::vector<std::string> pvs = buildPVs(prhs[0], wasCell, err);
    errCheck(err);

    std::vector<double> prec;
    for (size_t i = 0; i < pvs.size(); ++i) {
        epics::pvData::PVStructurePtr pv = pvaGet(pvs[i], "field(display)", err);
        if (err.err != PVA_OK) break;
        prec.push_back(getDoubleField(pv, "display.precision", 0.0));
    }
    errCheck(err);
    plhs[0] = colFromDoubles(prec, wasCell);
}
