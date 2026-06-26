/* pvaGetTimeout.cpp - read the connect/IO timeout (cf. lcaGetTimeout).
 *   t = pvaGetTimeout()
 */
#include "mglue.h"
#include "pvaGlue.h"
using namespace labpva;
void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    (void)nrhs; (void)prhs; (void)nlhs;
    plhs[0] = mxCreateDoubleScalar(pvaGetTimeout());
}
