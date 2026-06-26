/* pvaLastError.cpp - read the most recent error (cf. lcaLastError).
 *
 *   code        = pvaLastError()
 *   [code, msg] = pvaLastError()
 *
 * code is 0 (labpva:ok) when the last operation succeeded.
 */
#include "mglue.h"
using namespace labpva;
void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    (void)nrhs; (void)prhs;
    plhs[0] = mxCreateDoubleScalar((double)pvaLastErrorCode());
    if (nlhs >= 2)
        plhs[1] = mxCreateString(pvaLastErrorMsg().c_str());
}
