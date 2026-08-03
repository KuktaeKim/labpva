/* pvaGetEnumStrings.cpp - enum choice strings (cf. lcaGetEnumStrings).
 *
 *   c = pvaGetEnumStrings(pvname)            1 x K cell of choice strings
 *   c = pvaGetEnumStrings({pv1,pv2,...})     N x 1 cell, each a 1 x K cell
 *
 * Reads the `value.choices` of an NTEnum. Non-enum channels yield an empty
 * cell.
 */
#include "pvaMetaShared.h"
using namespace labpva;

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    (void)nlhs;
    if (nrhs < 1)
        mexErrMsgIdAndTxt("labpva:invalidArg", "pvaGetEnumStrings: need a PV name");
    PvaError err;
    bool wasCell = false;
    std::vector<std::string> pvs = buildPVs(prhs[0], wasCell, err);
    errCheck(err);

    std::vector<mxArray *> out;
    for (size_t i = 0; i < pvs.size(); ++i) {
        PvValue pv = pvaGet(pvs[i], "field(value)", err);
        if (err.err != PVA_OK) break;
        out.push_back(pvEnumChoicesToMx(pv));
    }
    errCheck(err);

    if (!wasCell) {
        plhs[0] = out.empty() ? mxCreateCellMatrix(1, 0) : out[0];
    } else {
        mxArray *cell = mxCreateCellMatrix(out.size(), 1);
        for (size_t i = 0; i < out.size(); ++i) mxSetCell(cell, i, out[i]);
        plhs[0] = cell;
    }
}
