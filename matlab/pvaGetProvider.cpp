/* pvaGetProvider.cpp - read the current per-channel provider token.
 *   p = pvaGetProvider()       'pva' or 'ca'
 */
#include "mglue.h"
#include "pvaGlue.h"
using namespace labpva;
void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    (void)nrhs; (void)prhs; (void)nlhs;
    plhs[0] = mxCreateString(pvaGetProvider().c_str());
}
