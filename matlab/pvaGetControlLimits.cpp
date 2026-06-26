/* pvaGetControlLimits.cpp - drive-range limits (cf. lcaGetControlLimits).
 *   [lo, hi] = pvaGetControlLimits(pvname[s])    from the NT `control` field
 */
#include "pvaMetaShared.h"
using namespace labpva;
void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    limitPairMex(nlhs, plhs, nrhs, prhs, "control.limitLow", "control.limitHigh");
}
