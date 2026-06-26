/* pvaSetTimeout.cpp - set the connect/IO timeout in seconds (cf. lcaSetTimeout).
 *   pvaSetTimeout(seconds)
 */
#include "mglue.h"
#include "pvaGlue.h"
using namespace labpva;
void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    (void)nlhs; (void)plhs;
    if (nrhs < 1 || !mxIsNumeric(prhs[0]) || mxGetNumberOfElements(prhs[0]) < 1)
        mexErrMsgIdAndTxt("labpva:invalidArg", "pvaSetTimeout: need a numeric seconds value");
    pvaSetTimeout(mxGetScalar(prhs[0]));
}
