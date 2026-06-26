/* pvaPutStructure.cpp - write a whole PVStructure from a MATLAB struct.
 *
 *   pvaPutStructure(pvname, s)              s mirrors the channel's structure
 *   pvaPutStructure(pvname, s, request)     request = pvRequest (default "field()")
 *   pvaPutStructure({pv1,...}, {s1,...})    cell of structs for a list of PVs
 *
 * Only the fields present in `s` are written (matched by sanitised field
 * name); fields omitted from `s` keep their fetched value. The whole
 * structure is marked changed before sending.
 */
#include "mglue.h"
#include "pvaGlue.h"
#include "pvaConvert.h"

using namespace labpva;
using namespace epics::pvData;
using namespace epics::pvaClient;

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    (void)nlhs; (void)plhs;
    if (nrhs < 2)
        mexErrMsgIdAndTxt("labpva:invalidArg",
                          "pvaPutStructure: need a PV name and a struct");

    PvaError err;
    bool wasCell = false;
    std::vector<std::string> pvs = buildPVs(prhs[0], wasCell, err);
    errCheck(err);

    const mxArray *sArg = prhs[1];
    std::string request = (nrhs >= 3) ? argString(prhs[2]) : "field()";
    if (request.empty()) request = "field()";

    if (!wasCell && !mxIsStruct(sArg))
        mexErrMsgIdAndTxt("labpva:invalidArg", "pvaPutStructure: value must be a struct");
    if (wasCell && !(mxIsCell(sArg) && mxGetNumberOfElements(sArg) == pvs.size()))
        mexErrMsgIdAndTxt("labpva:invalidArg",
                          "pvaPutStructure: for a list of PVs pass a matching cell of structs");

    for (size_t i = 0; i < pvs.size(); ++i) {
        const mxArray *smx = wasCell ? mxGetCell(sArg, i) : sArg;
        if (!smx || !mxIsStruct(smx)) {
            err.err = PVA_INVALIDARG; err.msg = "each value must be a struct"; break;
        }
        PvaClientPutPtr put = pvaPutPrepare(pvs[i], request, err);
        if (err.err != PVA_OK) break;
        PVStructurePtr data = put->getData()->getPVStructure();
        mxToPvField(smx, data, err);
        if (err.err != PVA_OK) break;
        pvaPutCommit(put, /*whole structure*/0, /*wait=*/true, err);
        if (err.err != PVA_OK) break;
    }
    errCheck(err);
}
