/* pvaDebugOff.cpp - disable pvaClient + labpva debug output (cf. lcaDebugOff).
 *   pvaDebugOff()
 */
#include "mglue.h"
#include "pvaGlue.h"
using namespace labpva;
void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    (void)nlhs; (void)plhs; (void)nrhs; (void)prhs;
    pvaSetDebug(false);
}
