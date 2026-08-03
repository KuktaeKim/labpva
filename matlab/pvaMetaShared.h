/* pvaMetaShared.h - helpers for the metadata MEX functions
 * (pvaGetControlLimits, pvaGetUnits, ...). These pull standard NT property
 * sub-fields (display/control/valueAlarm) out of the channel's structure.
 *
 * Note vs labca: in Channel Access these come from DBR_CTRL_* metadata
 * requests; in pvAccess they are just ordinary sub-fields of the NT structure,
 * so we fetch the whole structure once and read the paths.
 */
#ifndef LABPVA_META_SHARED_H
#define LABPVA_META_SHARED_H

#include "mglue.h"
#include "pvaGlue.h"
#include "pvaConvert.h"   /* getDoubleField/getStringField + PvValue */

namespace labpva {

static inline mxArray *colFromDoubles(const std::vector<double> &v, bool wasCell)
{
    if (!wasCell) return mxCreateDoubleScalar(v.empty() ? mxGetNaN() : v[0]);
    mxArray *m = mxCreateDoubleMatrix(v.size(), 1, mxREAL);
    double *p = mxGetPr(m);
    for (size_t i = 0; i < v.size(); ++i) p[i] = v[i];
    return m;
}

static inline mxArray *cellOrCharFromStrings(const std::vector<std::string> &v, bool wasCell)
{
    if (!wasCell) return mxCreateString(v.empty() ? "" : v[0].c_str());
    mxArray *c = mxCreateCellMatrix(v.size(), 1);
    for (size_t i = 0; i < v.size(); ++i) mxSetCell(c, i, mxCreateString(v[i].c_str()));
    return c;
}

/* Shared body for the four [low,high] limit getters. */
static inline void limitPairMex(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[],
                                const char *loPath, const char *hiPath)
{
    if (nrhs < 1)
        mexErrMsgIdAndTxt("labpva:invalidArg", "need at least a PV name");
    PvaError err;
    bool wasCell = false;
    std::vector<std::string> pvs = buildPVs(prhs[0], wasCell, err);
    errCheck(err);

    /* Fetch only the sub-structure we read (loPath/hiPath share a top field,
     * e.g. "control"), so a large PV (an NTNDArray image) is not pulled whole
     * just for two scalars. */
    std::string lp = loPath;
    size_t dot = lp.find('.');
    std::string request = (dot == std::string::npos)
                          ? std::string("field()")
                          : ("field(" + lp.substr(0, dot) + ")");

    std::vector<double> lo, hi;
    for (size_t i = 0; i < pvs.size(); ++i) {
        PvValue pv = pvaGet(pvs[i], request, err);
        if (err.err != PVA_OK) break;
        lo.push_back(getDoubleField(pv, loPath, mxGetNaN()));
        hi.push_back(getDoubleField(pv, hiPath, mxGetNaN()));
    }
    errCheck(err);

    plhs[0] = colFromDoubles(lo, wasCell);
    if (nlhs >= 2) plhs[1] = colFromDoubles(hi, wasCell);
}

} // namespace labpva

#endif /* LABPVA_META_SHARED_H */
