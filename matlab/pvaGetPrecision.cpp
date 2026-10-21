/* pvaGetPrecision.cpp - display precision (cf. lcaGetPrecision).
 *   p = pvaGetPrecision(pvname[s])
 *
 * Read from the NT `display.precision` field. The Channel Access provider
 * (pvAccessCA) has no such field: it encodes the precision in
 * `display.format` instead, as "F<prec+6>.<prec>" for float/double channels
 * and "I4"/"I6"/"I12" for char/short/long ones, so fall back to parsing that.
 * NaN when neither is present (e.g. an NTEnum), like the limit getters.
 */
#include <cctype>
#include <cstdlib>
#include "pvaMetaShared.h"
using namespace labpva;

/* "F<w>.<p>" -> p, "I<w>" -> 0, anything else -> NaN. */
static double precisionFromFormat(const std::string &fmt)
{
    if (fmt.size() < 2 || (fmt[0] != 'F' && fmt[0] != 'I')) return mxGetNaN();
    const char *s = fmt.c_str() + 1;
    char *end;
    if (!isdigit((unsigned char)*s)) return mxGetNaN();
    strtol(s, &end, 10);
    if (fmt[0] == 'I') return *end == '\0' ? 0.0 : mxGetNaN();
    if (*end != '.') return mxGetNaN();
    s = end + 1;
    long p = strtol(s, &end, 10);
    if (end == s || *end != '\0') return mxGetNaN();
    return (double)p;
}

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    (void)nlhs;
    if (nrhs < 1)
        mexErrMsgIdAndTxt("labpva:invalidArg", "pvaGetPrecision: need a PV name");
    PvaError err;
    bool wasCell = false;
    std::vector<std::string> pvs = buildPVs(prhs[0], wasCell, err);
    errCheck(err);

    std::vector<double> prec;
    for (size_t i = 0; i < pvs.size(); ++i) {
        PvValue pv = pvaGet(pvs[i], "field(display)", err);
        if (err.err != PVA_OK) break;
        double p = getDoubleField(pv, "display.precision", mxGetNaN());
        if (mxIsNaN(p)) p = precisionFromFormat(getStringField(pv, "display.format"));
        prec.push_back(p);
    }
    errCheck(err);
    plhs[0] = colFromDoubles(prec, wasCell);
}
