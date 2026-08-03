/* pvaBackend.h - selects the pvAccess client backend at compile time.
 *
 * labpva's glue can be built against either of two implementations of the
 * pvAccess protocol (chosen by defining PVXS in configure/RELEASE, which sets
 * -DLABPVA_USE_PVXS):
 *
 *   pvac (default) : pvaClient / pvAccessCPP / pvDataCPP -- the classic
 *                    EPICS 7 base stack. Value type: PVStructurePtr.
 *   pvxs           : PVXS (QSRV2-era library). Value type: pvxs::Value.
 *
 * `labpva::PvValue` is the ONE type that crosses the glue boundary: the MEX
 * entry points and the shared headers use it exclusively, so they compile
 * unchanged under either backend. Both types are cheap-to-copy handles that
 * are boolean-testable (`if (!pv)`) -- a null shared_ptr / an invalid Value.
 *
 * Backend-specific code lives ONLY in pvaGlue_<backend>.cpp and
 * pvaConvert_<backend>.cpp (picked by the Makefiles via LABPVA_BACKEND).
 */
#ifndef LABPVA_BACKEND_H
#define LABPVA_BACKEND_H

#ifdef LABPVA_USE_PVXS

#include <pvxs/data.h>
namespace labpva {
typedef ::pvxs::Value PvValue;
}

#else /* classic pvaClient / pvDataCPP backend */

#include <pv/pvData.h>
namespace labpva {
typedef ::epics::pvData::PVStructurePtr PvValue;
}

#endif

#endif /* LABPVA_BACKEND_H */
