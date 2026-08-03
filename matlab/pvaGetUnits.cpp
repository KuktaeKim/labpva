/* pvaGetUnits.cpp - engineering units string (cf. lcaGetUnits).
 *   u = pvaGetUnits(pvname)            char
 *   u = pvaGetUnits({pv1,pv2,...})     N x 1 cell of char
 * Read from the NT `display.units` field.
 */
#include "pvaMetaShared.h"
using namespace labpva;
void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    (void)nlhs;
    if (nrhs < 1)
        mexErrMsgIdAndTxt("labpva:invalidArg", "pvaGetUnits: need a PV name");
    PvaError err;
    bool wasCell = false;
    std::vector<std::string> pvs = buildPVs(prhs[0], wasCell, err);
    errCheck(err);

    std::vector<std::string> units;
    for (size_t i = 0; i < pvs.size(); ++i) {
        PvValue pv = pvaGet(pvs[i], "field(display)", err);
        if (err.err != PVA_OK) break;
        units.push_back(getStringField(pv, "display.units"));
    }
    errCheck(err);
    plhs[0] = cellOrCharFromStrings(units, wasCell);
}
