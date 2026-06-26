/* pvaError.cpp - see pvaError.h */
#include "pvaError.h"

namespace labpva {

const char *pvaErrorId(int code)
{
    switch (code) {
    case PVA_OK:           return "labpva:ok";
    case PVA_INVALIDARG:   return "labpva:invalidArg";
    case PVA_FAILEDMALLOC: return "labpva:failedMalloc";
    case PVA_FAILURE:      return "labpva:failure";
    case PVA_NOTCONNECTED: return "labpva:notConnected";
    case PVA_TIMEOUT:      return "labpva:timeout";
    case PVA_ABORTED:      return "labpva:aborted";
    case PVA_INTERNAL:     return "labpva:internal";
    case PVA_NOMONITOR:    return "labpva:noMonitor";
    case PVA_NOCHANNEL:    return "labpva:noChannel";
    case PVA_NOFIELD:      return "labpva:noField";
    case PVA_TYPEMISMATCH: return "labpva:typeMismatch";
    case PVA_UNSUPPORTED:  return "labpva:unsupported";
    default:               return "labpva:error";
    }
}

/* Process-global last error. labca keeps this per-MATLAB-process too; a
 * single MATLAB instance is single-threaded at the M-code level, so a plain
 * static is sufficient and matches labca's behaviour. */
static int         g_lastCode = PVA_OK;
static std::string g_lastMsg;

void pvaSetLastError(int code, const std::string &msg)
{
    g_lastCode = code;
    g_lastMsg  = msg;
}

int         pvaLastErrorCode() { return g_lastCode; }
std::string pvaLastErrorMsg()  { return g_lastMsg;  }

} // namespace labpva
