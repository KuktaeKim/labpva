/* pvaPut.cpp - write value(s) and wait for server completion (cf. lcaPut).
 *
 *   pvaPut(pvname, value)
 *   pvaPut(pvname, value, type)              type in 'NBSLFDC'
 *   pvaPut({pv1,pv2,...}, values [,type])    values = numeric vector or cell
 *
 * Blocks until each put's completion callback fires. For enum channels a
 * string value is matched against the choice list; a numeric value sets the
 * index. Use pvaPutNoWait for fire-and-forget, pvaPutStructure for whole
 * structures.
 */
#include "pvaPutShared.h"

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    labpva::pvaPutMexBody(nlhs, plhs, nrhs, prhs, /*wait=*/true);
}
