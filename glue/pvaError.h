/* pvaError.h - error handling for labpva
 *
 * Mirrors labca's lcaError.h conventions: a small error record that every
 * glue call fills in, plus a process-global "last error" that the
 * pvaLastError() MEX function can read back (analogue of lcaLastError).
 *
 * Unlike Channel Access / ezca, pvAccess surfaces failures as C++ exceptions
 * (std::runtime_error from pvaClient) and as epics::pvData::Status objects.
 * The glue layer catches both and funnels them through PvaError so the MEX
 * layer has a single, uniform reporting path.
 */
#ifndef PVA_ERROR_H
#define PVA_ERROR_H

#include <string>

namespace labpva {

/* Error codes. The low numbers intentionally echo ezca.h so anyone who knows
 * labca recognises them; the PVA-specific ones start at 30. */
enum PvaErrCode {
    PVA_OK              = 0,
    PVA_INVALIDARG      = 1,   /* bad MATLAB argument                       */
    PVA_FAILEDMALLOC    = 2,   /* out of memory                            */
    PVA_FAILURE         = 3,   /* generic pvAccess failure                 */
    PVA_NOTCONNECTED    = 5,   /* channel did not connect within timeout   */
    PVA_TIMEOUT         = 6,   /* operation timed out                      */
    PVA_ABORTED         = 9,   /* user abort (Ctrl-C)                      */
    PVA_INTERNAL        = 10,  /* internal/unexpected error                */
    PVA_NOMONITOR       = 20,  /* no monitor set on that channel           */
    PVA_NOCHANNEL       = 21,  /* channel not found in cache               */
    PVA_NOFIELD         = 30,  /* requested sub-field does not exist       */
    PVA_TYPEMISMATCH    = 31,  /* cannot convert between MATLAB and PV type */
    PVA_UNSUPPORTED     = 32   /* structure shape not representable        */
};

/* Per-call error record. `nerrs`/`errs` hold one code per PV when an
 * operation spans many channels, matching labca's per-PV error vector. */
struct PvaError {
    int          err;          /* aggregate code (first non-OK, or OK)     */
    std::string  msg;          /* human-readable detail                    */
    int          nerrs;        /* length of errs (0 if not a multi-PV op)  */
    int         *errs;         /* per-PV codes, owned by caller's arena    */

    PvaError() : err(PVA_OK), nerrs(0), errs(0) {}
};

/* Stable string ID for a code, used as the MATLAB error identifier
 * (e.g. "labpva:notConnected") so callers can try/catch on it. */
const char *pvaErrorId(int code);

/* Record an error as the process-global "last error" (read by pvaLastError).
 * Safe to call from anywhere in the glue/MEX layers. */
void pvaSetLastError(int code, const std::string &msg);

/* Read back the last recorded error. */
int         pvaLastErrorCode();
std::string pvaLastErrorMsg();

} // namespace labpva

#endif /* PVA_ERROR_H */
