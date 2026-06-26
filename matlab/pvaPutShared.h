/* pvaPutShared.h - shared body for pvaPut (wait) and pvaPutNoWait (no wait).
 *
 * Header-only so each MEX file stays a single translation unit (labca splits
 * the same way via theLcaPutMexFunction). Writes only the `value` field of
 * each channel's structure; use pvaPutStructure for whole-structure writes.
 */
#ifndef LABPVA_PUT_SHARED_H
#define LABPVA_PUT_SHARED_H

#include "mglue.h"
#include "pvaGlue.h"
#include "pvaConvert.h"

namespace labpva {

/* Select the per-PV MATLAB value from the value argument.
 *   single PV     -> the whole argument
 *   list + cell   -> element i
 *   list + numeric (numel==N) -> a 1x1 double holding element i  (caller frees)
 * Returns NULL and sets err on a shape mismatch. */
static inline const mxArray *
putValueFor(const mxArray *valArg, size_t i, size_t n, bool wasCell,
            mxArray **scratch, PvaError &err)
{
    *scratch = NULL;
    if (!wasCell) return valArg;
    if (mxIsCell(valArg)) {
        if (mxGetNumberOfElements(valArg) != n) {
            err.err = PVA_INVALIDARG;
            err.msg = "value cell length must match the number of PVs";
            return NULL;
        }
        return mxGetCell(valArg, i);
    }
    if ((mxIsNumeric(valArg) || mxIsLogical(valArg)) &&
        mxGetNumberOfElements(valArg) == n) {
        mxArray *s = mxCreateDoubleScalar(mxGetScalar(valArg)); /* placeholder */
        mxGetPr(s)[0] = mxIsDouble(valArg) ? mxGetPr(valArg)[i] : mxGetScalar(valArg);
        *scratch = s;
        return s;
    }
    err.err = PVA_INVALIDARG;
    err.msg = "for a list of PVs, give a cell of values or a numeric vector "
              "with one element per PV";
    return NULL;
}

static inline void
pvaPutMexBody(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[], bool wait)
{
    (void)nlhs; (void)plhs;
    if (nrhs < 2)
        mexErrMsgIdAndTxt("labpva:invalidArg",
                          "pvaPut: need a PV name and a value");

    PvaError err;
    bool wasCell = false;
    std::vector<std::string> pvs = buildPVs(prhs[0], wasCell, err);
    errCheck(err);

    const mxArray *valArg = prhs[1];
    char type = (nrhs >= 3) ? parseTypeArg(prhs[2]) : 'N';
    size_t n = pvs.size();

    for (size_t i = 0; i < n; ++i) {
        mxArray *scratch = NULL;
        const mxArray *vmx = putValueFor(valArg, i, n, wasCell, &scratch, err);
        if (err.err != PVA_OK) break;

        epics::pvaClient::PvaClientPutPtr put =
            pvaPutPrepare(pvs[i], "field(value)", err);
        if (err.err != PVA_OK) { if (scratch) mxDestroyArray(scratch); break; }

        epics::pvData::PVStructurePtr data = put->getData()->getPVStructure();
        std::string wf = mxToPvValue(vmx, data, type, err);
        if (err.err == PVA_OK) {
            epics::pvData::PVFieldPtr vf = data->getSubField(wf);
            std::size_t off = vf ? vf->getFieldOffset() : 0;
            pvaPutCommit(put, off, wait, err);
        }
        if (scratch) mxDestroyArray(scratch);
        if (err.err != PVA_OK) break;
    }
    errCheck(err);
}

} // namespace labpva

#endif /* LABPVA_PUT_SHARED_H */
