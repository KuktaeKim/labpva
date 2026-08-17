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
#include "pvaBackend.h"
#ifndef LABPVA_USE_PVXS
#include <pv/pvaClient.h>
#endif
#include <string>
#include <vector>

namespace labpva {

/* ---- backend link guard ----------------------------------------------
 * Each backend's glue defines exactly ONE of these; mglue's lockMexFile
 * references the one matching its own compile-time backend. If a MEX compiled
 * for one backend is loaded against a libmpvaglue.so built for the other
 * (e.g. after toggling PVXS in configure/RELEASE without `make clean`), the
 * MEX fails to load with a clear "undefined symbol" error instead of silently
 * mis-treating PvValue (whose C++ mangling does NOT differ for return types,
 * so pvaGet would otherwise resolve and corrupt memory). */
#ifdef LABPVA_USE_PVXS
void backendGuardPvxs();
#else
void backendGuardPvac();
#endif

/* ---- configuration (analogues of lcaSet/GetTimeout etc.) ------------- */

/* Provider token for subsequently-opened channels. On the classic backend
 * "pva" (default) or "ca" (Channel Access fallback for v3-only names). Returns
 * false if this backend cannot provide the requested protocol -- the PVXS
 * backend speaks pvAccess only, so it refuses "ca" (the MEX then raises a
 * clear labpva:unsupported error). */
bool        pvaSetProvider(const std::string &providers);
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
PvValue
pvaGet(const std::string &name, const std::string &request, PvaError &err,
       bool useMonitorCache = false, bool requireWholeMonitor = false);

#ifndef LABPVA_USE_PVXS
/* ---- write path (classic backend; the PVXS equivalent lands with the put
 * phase of the port -- pvxs puts are builder-based, a different shape) ----- */

/* Prepare a put: connect, create the put, and fetch current values so the
 * caller can modify just the fields it wants. Returns null on failure. */
epics::pvaClient::PvaClientPutPtr
pvaPutPrepare(const std::string &name, const std::string &request, PvaError &err);

/* Commit a prepared put. `wait` true => block for server completion
 * (lcaPut); false => fire-and-forget (lcaPutNoWait). Marks the given field
 * offset (or the whole structure if fieldOffset==0) changed before sending. */
void pvaPutCommit(const epics::pvaClient::PvaClientPutPtr &put,
                  std::size_t changedFieldOffset, bool wait, PvaError &err);
#else
/* ---- write path (PVXS backend) --------------------------------------- */

/* The type template a put argument is built from (pvaConvert.h mxToPutArg*).
 * For an ordinary PV it is the warm put operation's INIT prototype -- the
 * type description pvxs gets from the server when the (reused) operation is
 * created -- so a scalar/waveform put needs NO read at all. An enum is the
 * one exception: its `choices` are data rather than type AND can change
 * server-side without a reconnect, so every enum put reads them fresh (one
 * round trip over the warm read channel), never from a cache -- a stale list
 * would silently write a wrong index. Returns an invalid Value with `err`
 * set on failure. */
PvValue pvaPutProto(const std::string &name, PvaError &err);

/* Execute a put of a pre-built argument Value (see pvaConvert.h mxToPutArg*):
 * only the MARKED fields of `arg` are sent; the server keeps the rest. Both
 * modes use the channel's warm put operation when it is verifiably ready
 * (alive, free, connected, matching type) -- one round trip, no per-put
 * setup -- and a one-shot operation otherwise. `wait` true blocks for
 * completion (lcaPut); false is fire-and-forget (lcaPutNoWait; an in-flight
 * one-shot handle is parked internally so it is not cancelled, and reaped
 * once its completion callback fires).
 *
 * Delivery contract: a put is NEVER re-executed automatically. On a timeout
 * or a mid-put disconnect the write may or may not have been applied (only
 * the acknowledgement is known lost); labpva reports the error -- timeouts as
 * labpva:timeout on every path -- and leaves the retry decision to the
 * caller, because puts are not idempotent (.PROC, relative moves). */
void pvaPutExec(const std::string &name, const PvValue &arg, bool wait, PvaError &err);
#endif /* !LABPVA_USE_PVXS */

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

/* The most recently cached monitored value (deep copy), or invalid if none.
 * pvaGet uses this so a monitored channel is served from cache like ezca. */
PvValue pvaMonitorLatest(const std::string &name);

bool pvaMonitorActive(const std::string &name);

/* Names of all channels with an active monitor (backs the pvaMonitors verb). */
std::vector<std::string> pvaMonitorNames();

/* Names of all channels labpva has opened this session (backs pvaChannels). */
std::vector<std::string> pvaChannelNames();

/* Is the (previously opened) channel for `name` currently connected? False if
 * labpva never opened it -- does NOT open a new channel just to check. */
bool pvaChannelConnected(const std::string &name);

/* Clear one channel's monitor (stop it + drop it from the registry), or
 * (empty name) clear all monitors. The underlying pvaClient channel stays in
 * pvaClient's own cache -- the connection persists; only the monitor
 * subscription is torn down. After clearing, pvaGet does a fresh server read
 * again. Mirrors lcaClear's monitor teardown. (The PVXS backend additionally
 * retires the channel's cached put operation and type template -- again
 * without dropping the connection; the next put rebuilds them.) */
void pvaClear(const std::string &name);

} // namespace labpva

#endif /* PVA_GLUE_H */
