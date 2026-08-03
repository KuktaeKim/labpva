/* pvaInfo.cpp - introspect a channel (cf. the `pvinfo` CLI tool).
 *
 *   s = pvaInfo(pvname)
 *
 * Returns a struct with:
 *   .name           the channel name
 *   .typeid         the structure's normative-type ID (e.g. "epics:nt/NTScalar:1.0")
 *   .introspection  a text dump of the field tree with current values
 *
 * Useful before writing port code: it tells you the exact shape pvaGet /
 * pvaGetStructure will hand back.
 */
#include "mglue.h"
#include "pvaGlue.h"
#include "pvaConvert.h"

using namespace labpva;

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    (void)nlhs;
    if (nrhs < 1 || !mxIsChar(prhs[0]))
        mexErrMsgIdAndTxt("labpva:invalidArg", "pvaInfo: need a single PV name");

    lockMexFile();          /* about to do EPICS channel work -- pin the MEX */
    std::string name = argString(prhs[0]);
    PvaError err;
    PvValue pv = pvaGet(name, "field()", err);
    errCheck(err);

    const char *fn[3] = { "name", "typeid", "introspection" };
    mxArray *s = mxCreateStructMatrix(1, 1, 3, fn);
    mxSetFieldByNumber(s, 0, 0, mxCreateString(name.c_str()));
    mxSetFieldByNumber(s, 0, 1, mxCreateString(pvTypeId(pv).c_str()));
    mxSetFieldByNumber(s, 0, 2, mxCreateString(pvIntrospect(pv).c_str()));
    plhs[0] = s;
}
