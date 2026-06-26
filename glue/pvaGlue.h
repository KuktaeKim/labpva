/* pvaGlue.h - pvaClient lifecycle, channel cache and monitor registry
 *
 * This is the labpva analogue of labca's multiEzca.c + the ezca channel
 * cache. It owns the single process-wide PvaClient (the synchronous wrapper
 * over the callback-based pvAccess API), reuses pvaClient's own channel cache
 * for connections, and keeps a registry of monitors keyed by channel name so
 * pvaSetMonitor / pvaNewMonitorValue / pvaNewMonitorWait behave like labca's
 * lcaSetMonitor / lcaNewMonitorValue / lcaNewMonitorWait.
 *
 * Every entry point catches pvaClient's std::runtime_error and pvData Status
 * failures and reports them through PvaError (and the process-global last
 * error) rather than letting an exception escape into MATLAB.
 */
#ifndef PVA_GLUE_H
#define PVA_GLUE_H

#include "pvaError.h"
#include <pv/pvaClient.h>
#include <string>

namespace labpva {

/* ---- configuration (analogues of lcaSet/GetTimeout etc.) ------------- */

/* Provider order handed to PvaClient. Default "pva ca" means: try pvAccess
 * first, fall back to Channel Access for names a v3 IOC still serves. Set to
 * "pva" to force pvAccess only, or "ca" to force Channel Access. */
void        pvaSetProvider(const std::string &providers);
std::string pvaGetProvider();

void        pvaSetTimeout(double seconds);   /* connect/IO timeout, default 5 */
double      pvaGetTimeout();

void        pvaSetDebug(bool on);            /* routes pvaClient debug + ours */
bool        pvaGetDebug();

/* ---- core operations ------------------------------------------------- */

/* Connect (cached) and read. Returns a private deep copy of the channel's
 * PVStructure (safe to hold and marshal), or null on failure with `err` set.
 * `request` is a pvRequest string such as "field(value,alarm,timeStamp)".
 *
 * `useMonitorCache` (default false) opts into ezca-style serving: if a monitor
 * is active on `name`, return its cached latest value with no network read.
 *
 * `requireWholeMonitor` (default false) guards that cache for structure reads:
 * when true, the cached value is served ONLY if the monitor's pvRequest covers
 * the requested fields (its request is whole `field()` / empty, or identical to
 * `request`); otherwise it falls through to a fresh read. This stops an active
 * monitor whose request omits display/control/valueAlarm from silently feeding
 * a partial structure (NaN/""/0 metadata). The `pvaGet` value verb leaves it
 * false (its needs -- just `value` -- are met by any monitor); `pvaGetStructure`
 * sets it true. Metadata getters leave `useMonitorCache` false and always read
 * fresh. */
epics::pvData::PVStructurePtr
pvaGet(const std::string &name, const std::string &request, PvaError &err,
       bool useMonitorCache = false, bool requireWholeMonitor = false);

/* Prepare a put: connect, create the put, and fetch current values so the
 * caller can modify just the fields it wants. Returns null on failure. */
epics::pvaClient::PvaClientPutPtr
pvaPutPrepare(const std::string &name, const std::string &request, PvaError &err);

/* Commit a prepared put. `wait` true => block for server completion
 * (lcaPut); false => fire-and-forget (lcaPutNoWait). Marks the given field
 * offset (or the whole structure if fieldOffset==0) changed before sending. */
void pvaPutCommit(const epics::pvaClient::PvaClientPutPtr &put,
                  std::size_t changedFieldOffset, bool wait, PvaError &err);

/* ---- monitor registry (lcaSetMonitor family) ------------------------ */

/* Subscribe to a channel (replaces any existing monitor on that name). */
void pvaMonitorSet(const std::string &name, const std::string &request, PvaError &err);

/* Non-blocking: drain pending monitor events keeping the latest, cache it,
 * and return true iff at least one new value arrived since the last call that
 * consumed it. Mirrors lcaNewMonitorValue. */
bool pvaMonitorPoll(const std::string &name, PvaError &err);

/* Blocking up to `timeout` seconds (0 => use the configured default) for the
 * next monitor event. Mirrors lcaNewMonitorWait. Returns true on event. */
bool pvaMonitorWait(const std::string &name, double timeout, PvaError &err);

/* The most recently cached monitored value (deep copy), or null if none.
 * pvaGet uses this so a monitored channel is served from cache like ezca. */
epics::pvData::PVStructurePtr pvaMonitorLatest(const std::string &name);

bool pvaMonitorActive(const std::string &name);

/* Clear one channel's monitor (stop it + drop it from the registry), or
 * (empty name) clear all monitors. The underlying pvaClient channel stays in
 * pvaClient's own cache -- the connection persists; only the monitor
 * subscription is torn down. After clearing, pvaGet does a fresh server read
 * again. Mirrors lcaClear's monitor teardown. */
void pvaClear(const std::string &name);

} // namespace labpva

#endif /* PVA_GLUE_H */
