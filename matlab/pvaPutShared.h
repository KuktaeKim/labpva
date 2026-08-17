/* pvaPutShared.h - shared body for pvaPut (wait) and pvaPutNoWait (no wait).
 *
 * Header-only so each MEX file stays a single translation unit (labca splits
 * the same way via theLcaPutMexFunction). Writes only the `value` field of
 * each channel's structure; use pvaPutStructure for whole-structure writes.
 *
 * The MATLAB argument handling is backend-neutral; only the "perform the put"
 * step differs: the classic backend uses pvaClient's cached put handles
 * (pvaPutPrepare/pvaPutCommit + mxToPvValue), the PVXS backend pre-builds an
 * argument Value from the channel's cached type template (pvaPutProto +
 * mxToPutArg) and sends its marked fields over that channel's warm put
 * operation (pvaPutExec). Both are one server round trip per put.
 */
#ifndef LABPVA_PUT_SHARED_H
#define LABPVA_PUT_SHARED_H

#include "mglue.h"
#include "pvaGlue.h"
#include "pvaConvert.h"

namespace labpva {

/* Element i of any numeric/logical MATLAB array, as double. `ok` false for an
 * unsupported class (then the value is unusable). Element-accurate for every
 * numeric class -- mxGetScalar would silently return element 1 for all i. */
static inline double
numericElement(const mxArray *mx, size_t i, bool &ok)
{
    ok = true;
    if (mxIsLogical(mx)) return mxGetLogicals(mx)[i] ? 1.0 : 0.0;
    switch (mxGetClassID(mx)) {
    case mxDOUBLE_CLASS: return ((const double  *)mxGetData(mx))[i];
    case mxSINGLE_CLASS: return (double)((const float   *)mxGetData(mx))[i];
    case mxINT8_CLASS:   return (double)((const int8_T  *)mxGetData(mx))[i];
    case mxUINT8_CLASS:  return (double)((const uint8_T *)mxGetData(mx))[i];
    case mxINT16_CLASS:  return (double)((const int16_T *)mxGetData(mx))[i];
    case mxUINT16_CLASS: return (double)((const uint16_T*)mxGetData(mx))[i];
    case mxINT32_CLASS:  return (double)((const int32_T *)mxGetData(mx))[i];
    case mxUINT32_CLASS: return (double)((const uint32_T*)mxGetData(mx))[i];
    case mxINT64_CLASS:  return (double)((const int64_T *)mxGetData(mx))[i];
    case mxUINT64_CLASS: return (double)((const uint64_T*)mxGetData(mx))[i];
    default: ok = false; return 0.0;
    }
}

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
        if (mxIsComplex(valArg)) {
            err.err = PVA_INVALIDARG;
            err.msg = "complex values cannot be written to a PV";
            return NULL;
        }
        bool ok = false;
        double d = numericElement(valArg, i, ok);
        if (!ok) {
            err.err = PVA_INVALIDARG;
            err.msg = "unsupported numeric class in the value vector";
            return NULL;
        }
        mxArray *s = mxCreateDoubleScalar(d);
        *scratch = s;
        return s;
    }
    err.err = PVA_INVALIDARG;
    err.msg = "for a list of PVs, give a cell of values or a numeric vector "
              "with one element per PV";
    return NULL;
}

#ifdef LABPVA_USE_PVXS

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

        /* The put channel's type template lets the argument be built here, on
         * the MATLAB thread: for an ordinary PV it is the warm operation's
         * INIT prototype (no server read at all); for an enum it is a fresh
         * read, because the choice list is data that can change without a
         * reconnect -- see pvaPutProto. */
        PvValue cur = pvaPutProto(pvs[i], err);
        if (err.err == PVA_OK) {
            PvValue arg = mxToPutArg(vmx, cur, type, err);
            if (err.err == PVA_OK)
                pvaPutExec(pvs[i], arg, wait, err);
        }
        if (scratch) mxDestroyArray(scratch);
        if (err.err != PVA_OK) break;
    }
    errCheck(err);
}

#else /* classic backend */

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

#endif /* LABPVA_USE_PVXS */

} // namespace labpva

#endif /* LABPVA_PUT_SHARED_H */
