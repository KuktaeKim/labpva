/* pvaSetProvider.cpp - choose the per-channel provider (no labca analogue;
 * Channel Access has only one protocol).
 *
 *   pvaSetProvider('pva')      use pvAccess (default)
 *   pvaSetProvider('ca')       use Channel Access for subsequently-named channels
 *
 * Affects channels opened after the call; already-cached channels keep their
 * provider. Both providers are always available to the underlying client.
 */
#include "mglue.h"
#include "pvaGlue.h"
using namespace labpva;
void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    (void)nlhs; (void)plhs;
    if (nrhs < 1 || !mxIsChar(prhs[0]))
        mexErrMsgIdAndTxt("labpva:invalidArg", "pvaSetProvider: need 'pva' or 'ca'");
    pvaSetProvider(argString(prhs[0]));
}
