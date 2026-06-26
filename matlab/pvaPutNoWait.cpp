/* pvaPutNoWait.cpp - write value(s) without waiting for completion (cf.
 * lcaPutNoWait). Same arguments as pvaPut; returns as soon as the request is
 * issued.
 */
#include "pvaPutShared.h"

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    labpva::pvaPutMexBody(nlhs, plhs, nrhs, prhs, /*wait=*/false);
}
