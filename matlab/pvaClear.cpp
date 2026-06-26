/* pvaClear.cpp - tear down monitors / cached channels (cf. lcaClear).
 *
 *   pvaClear()                 clear all monitors
 *   pvaClear(pvname)           clear one channel's monitor
 *   pvaClear({pv1,pv2,...})    clear several
 */
#include "mglue.h"
#include "pvaGlue.h"

using namespace labpva;

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    (void)nlhs; (void)plhs;
    if (nrhs < 1) {                          /* clear everything */
        pvaClear("");
        return;
    }
    PvaError err;
    bool wasCell = false;
    std::vector<std::string> pvs = buildPVs(prhs[0], wasCell, err);
    errCheck(err);
    for (size_t i = 0; i < pvs.size(); ++i)
        pvaClear(pvs[i]);
}
