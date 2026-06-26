/* pvaGetAlarmLimits.cpp - alarm thresholds (cf. lcaGetAlarmLimits).
 *   [lo, hi] = pvaGetAlarmLimits(pvname[s])    from the NT `valueAlarm` field
 */
#include "pvaMetaShared.h"
using namespace labpva;
void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    limitPairMex(nlhs, plhs, nrhs, prhs,
                 "valueAlarm.lowAlarmLimit", "valueAlarm.highAlarmLimit");
}
