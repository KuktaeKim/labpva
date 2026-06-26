/* pvaGetGraphicLimits.cpp - display range hints (cf. lcaGetGraphicLimits).
 *   [lo, hi] = pvaGetGraphicLimits(pvname[s])    from the NT `display` field
 */
#include "pvaMetaShared.h"
using namespace labpva;
void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    limitPairMex(nlhs, plhs, nrhs, prhs, "display.limitLow", "display.limitHigh");
}
