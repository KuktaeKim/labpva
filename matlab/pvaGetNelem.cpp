/* pvaGetNelem.cpp - element count of the value field (cf. lcaGetNelem).
 *   n = pvaGetNelem(pvname[s])    1 for a scalar/enum, array length otherwise
 */
#include "pvaMetaShared.h"
using namespace labpva;
using namespace epics::pvData;
void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    (void)nlhs;
    if (nrhs < 1)
        mexErrMsgIdAndTxt("labpva:invalidArg", "pvaGetNelem: need a PV name");
    PvaError err;
    bool wasCell = false;
    std::vector<std::string> pvs = buildPVs(prhs[0], wasCell, err);
    errCheck(err);

    std::vector<double> nel;
    for (size_t i = 0; i < pvs.size(); ++i) {
        PVStructurePtr pv = pvaGet(pvs[i], "field(value)", err);
        if (err.err != PVA_OK) break;
        double n = 1.0;
        PVFieldPtr v = pv ? pv->getSubField("value") : PVFieldPtr();
        if (v && v->getField()->getType() == scalarArray)
            n = (double)std::tr1::static_pointer_cast<PVScalarArray>(v)->getLength();
        nel.push_back(n);
    }
    errCheck(err);
    plhs[0] = colFromDoubles(nel, wasCell);
}
