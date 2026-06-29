/* mglue.cpp - see mglue.h */
#include "mglue.h"

using namespace epics::pvData;

namespace labpva {

void lockMexFile()
{
    /* Lock this MEX in memory on first channel-touching call, so MATLAB does
     * NOT unload labpva -- and with it the EPICS client libraries -- while
     * pvAccess background threads are still alive. Without this, quitting MATLAB
     * after a get/clear that left a worker thread running segfaults: the thread
     * jumps into code that was just unmapped (the crash shows a thread in
     * clone/libpthread with a bad RIP). Mirrors labca's CONFIG_MEXLOCK. We never
     * unlock -- the process simply exits with the libs still mapped, like labca
     * (EPICS client state cannot be torn down cleanly mid-session). Idempotent:
     * locks at most once per MEX file. */
    static bool locked = false;
    if (!locked) { mexLock(); locked = true; }
}

std::string argString(const mxArray *mx)
{
    if (!mx || !mxIsChar(mx)) return "";
    char *c = mxArrayToString(mx);
    std::string s = c ? c : "";
    if (c) mxFree(c);
    return s;
}

std::vector<std::string> buildPVs(const mxArray *mx, bool &wasCell, PvaError &err)
{
    lockMexFile();          /* about to do EPICS channel work -- pin the MEX */
    std::vector<std::string> out;
    wasCell = false;
    if (!mx) {
        err.err = PVA_INVALIDARG;
        err.msg = "missing PV name argument";
        return out;
    }
    if (mxIsChar(mx)) {
        out.push_back(argString(mx));
    } else if (mxIsCell(mx)) {
        wasCell = true;
        size_t n = mxGetNumberOfElements(mx);
        out.reserve(n);
        for (size_t i = 0; i < n; ++i) {
            mxArray *c = mxGetCell(mx, i);
            if (!c || !mxIsChar(c)) {
                err.err = PVA_INVALIDARG;
                err.msg = "PV name cell must contain only char rows";
                out.clear();
                return out;
            }
            out.push_back(argString(c));
        }
    } else {
        err.err = PVA_INVALIDARG;
        err.msg = "PV name(s) must be a char row or a cell array of char";
    }
    return out;
}

char parseTypeArg(const mxArray *mx)
{
    std::string s = argString(mx);
    if (s.empty()) return 'N';
    char c = s[0];
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');   /* normalise to upper */
    switch (c) {
    case 'N': case 'B': case 'S': case 'L': case 'F': case 'D': case 'C':
        return c;
    default:
        return 'N';
    }
}

void errCheck(PvaError &err)
{
    if (err.err != PVA_OK) {
        pvaSetLastError(err.err, err.msg);
        mexErrMsgIdAndTxt(pvaErrorId(err.err), "%s", err.msg.c_str());
    }
}

mxArray *assembleValueOutput(std::vector<mxArray *> &values, bool wasCell)
{
    if (!wasCell) {
        return values.empty() ? mxCreateDoubleMatrix(0, 0, mxREAL) : values[0];
    }
    /* List input: numeric column if every element is a real 1x1 double. */
    bool allScalar = true;
    for (size_t i = 0; i < values.size(); ++i) {
        mxArray *v = values[i];
        if (!v || !mxIsDouble(v) || mxIsComplex(v) ||
            mxGetNumberOfElements(v) != 1) { allScalar = false; break; }
    }
    if (allScalar) {
        size_t n = values.size();
        mxArray *col = mxCreateDoubleMatrix(n, 1, mxREAL);
        double *p = mxGetPr(col);
        for (size_t i = 0; i < n; ++i) { p[i] = mxGetScalar(values[i]); mxDestroyArray(values[i]); }
        return col;
    }
    size_t n = values.size();
    mxArray *cell = mxCreateCellMatrix(n, 1);
    for (size_t i = 0; i < n; ++i) mxSetCell(cell, i, values[i]);
    return cell;
}

mxArray *complexColumn(const std::vector<double> &re, const std::vector<double> &im)
{
    size_t n = re.size();
    mxArray *m = mxCreateDoubleMatrix(n, 1, mxCOMPLEX);
#if MX_HAS_INTERLEAVED_COMPLEX
    mxComplexDouble *z = mxGetComplexDoubles(m);
    for (size_t i = 0; i < n; ++i) { z[i].real = re[i]; z[i].imag = im[i]; }
#else
    double *pr = mxGetPr(m);
    double *pi = mxGetPi(m);
    for (size_t i = 0; i < n; ++i) { pr[i] = re[i]; pi[i] = im[i]; }
#endif
    return m;
}

double getDoubleField(const PVStructurePtr &pv, const std::string &path, double dflt)
{
    if (!pv) return dflt;
    PVFieldPtr f = pv->getSubField(path);
    if (!f || f->getField()->getType() != scalar) return dflt;
    return std::tr1::static_pointer_cast<PVScalar>(f)->getAs<double>();
}

std::string getStringField(const PVStructurePtr &pv, const std::string &path)
{
    if (!pv) return "";
    PVFieldPtr f = pv->getSubField(path);
    if (!f || f->getField()->getType() != scalar) return "";
    return std::tr1::static_pointer_cast<PVScalar>(f)->getAs<std::string>();
}

} // namespace labpva
