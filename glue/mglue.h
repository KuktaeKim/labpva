/* mglue.h - MEX argument/return helpers shared by every labpva MEX function
 *
 * Analogue of labca's glue/mglue.h: turn the MATLAB argument list into C++,
 * funnel PvaError through mexErrMsgIdAndTxt, and assemble return values with
 * the same single-PV-vs-list shape conventions labca uses.
 */
#ifndef LABPVA_MGLUE_H
#define LABPVA_MGLUE_H

#include "mex.h"
#include "pvaError.h"
#include <pv/pvData.h>
#include <string>
#include <vector>

namespace labpva {

/* Lock this MEX file in memory (mexLock) on first call, so MATLAB does not
 * unload labpva -- and with it the EPICS client libraries -- while pvAccess
 * background threads are still running. Unloading mid-teardown segfaults (a
 * worker thread jumps into just-unmapped code). Mirrors labca's CONFIG_MEXLOCK.
 * Idempotent: locks at most once per MEX. Called from buildPVs (covers every
 * channel verb) and from pvaInfo (which parses its name directly). */
void lockMexFile();

/* Extract PV name(s) from a char row or a cell array of char. Sets
 * `wasCell` true iff the caller passed a cell (which fixes list-shaped
 * output even for one element). */
std::vector<std::string> buildPVs(const mxArray *mx, bool &wasCell, PvaError &err);

/* Parse a labca-style type letter ('N','B','S','L','F','D','C') from a
 * trailing string argument; returns 'N' (native) if absent/blank. */
char parseTypeArg(const mxArray *mx);

/* Read a char argument as std::string ("" if not a char array). */
std::string argString(const mxArray *mx);

/* If `err` is set, abort the MEX call with a MATLAB error whose identifier is
 * pvaErrorId(err.err). Records it as the last error first. */
void errCheck(PvaError &err);

/* Assemble per-PV results into one return value:
 *   single PV (wasCell==false)      -> values[0] verbatim
 *   list, all real 1x1 doubles      -> N x 1 double column (labca-compatible)
 *   list, anything else             -> N x 1 cell column
 * Takes ownership of the mxArrays in `values`. */
mxArray *assembleValueOutput(std::vector<mxArray *> &values, bool wasCell);

/* Build an N x 1 complex double column from parallel real/imag vectors (N==1
 * gives a 1 x 1 complex scalar). Used for labca-style timestamps
 * (sec + i*nsec). Works under both the classic (mxGetPi) and interleaved
 * (mxGetComplexDoubles) MATLAB complex APIs. */
mxArray *complexColumn(const std::vector<double> &re, const std::vector<double> &im);

/* Read a scalar double sub-field by (possibly dotted) path, e.g.
 * "display.limitLow"; returns `dflt` if the field is absent. */
double getDoubleField(const epics::pvData::PVStructurePtr &pv,
                      const std::string &path, double dflt);

/* Read a string sub-field by path; returns "" if absent. */
std::string getStringField(const epics::pvData::PVStructurePtr &pv,
                           const std::string &path);

} // namespace labpva

#endif /* LABPVA_MGLUE_H */
