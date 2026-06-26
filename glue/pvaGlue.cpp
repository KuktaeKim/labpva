/* pvaGlue.cpp - see pvaGlue.h */
#include "pvaGlue.h"

#include <pv/pvData.h>
#include <pv/bitSet.h>

#include <cctype>
#include <map>
#include <stdexcept>

using namespace epics::pvData;
using namespace epics::pvaClient;

namespace labpva {

/* ---- configuration state -------------------------------------------- */

static std::string g_provider = "pva";   /* per-channel provider token   */
static double      g_timeout  = 5.0;
static bool        g_debug    = false;

void        pvaSetProvider(const std::string &p) { g_provider = p.empty() ? "pva" : p; }
std::string pvaGetProvider()                      { return g_provider; }
void        pvaSetTimeout(double s)               { if (s > 0) g_timeout = s; }
double      pvaGetTimeout()                       { return g_timeout; }
void        pvaSetDebug(bool on)                  { g_debug = on; PvaClient::setDebug(on); }
bool        pvaGetDebug()                          { return g_debug; }

/* ---- the single PvaClient ------------------------------------------- */

/* Created lazily with both providers available; the per-channel provider
 * token (g_provider) selects pva vs ca for each channel() call. */
static PvaClientPtr g_client;

static PvaClientPtr client()
{
    if (!g_client) g_client = PvaClient::get("pva ca");
    return g_client;
}

/* ---- monitor registry ----------------------------------------------- */

struct MonEntry {
    PvaClientMonitorPtr mon;
    PVStructurePtr      latest;   /* private deep copy of most recent value */
    std::string         request;  /* the pvRequest this monitor subscribed with */
    bool                unseen;   /* true if a value arrived but pvaNewMonitorValue
                                     has not yet reported it                */
    MonEntry() : unseen(false) {}
};

static std::map<std::string, MonEntry> g_monitors;

/* Does a monitor subscribed with `monReq` carry every field a get for `getReq`
 * asks for? A whole-structure monitor (`field()` / empty) covers any request;
 * otherwise we only trust an exact (whitespace-insensitive) request match. Used
 * to guard cache-served pvaGetStructure so it never returns a partial tree. */
static std::string stripWs(const std::string &s)
{
    std::string o;
    o.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i)
        if (!std::isspace((unsigned char)s[i])) o.push_back(s[i]);
    return o;
}

static bool monitorCovers(const std::string &monReq, const std::string &getReq)
{
    std::string m = stripWs(monReq);
    if (m.empty() || m == "field()" || m == "field") return true;
    return m == stripWs(getReq);
}

/* Deep-copy a PVStructure so we can hold it after releaseEvent / across the
 * MEX call boundary. The PVDataCreate clone overload copies both the
 * introspection and the current values. */
static PVStructurePtr deepCopy(const PVStructurePtr &src)
{
    return getPVDataCreate()->createPVStructure(src);
}

/* ---- core operations ------------------------------------------------- */

PVStructurePtr pvaGet(const std::string &name, const std::string &request, PvaError &err,
                      bool useMonitorCache, bool requireWholeMonitor)
{
    /* ezca-style serving: if a monitor is active on `name`, return its cached
     * value with no network round-trip (a read right after a positive
     * pvaNewMonitorValue returns the monitored sample). Both pvaGet (value) and
     * pvaGetStructure opt in. For structure reads `requireWholeMonitor` is true,
     * so the cache is used only when the monitor's request actually covers the
     * requested fields; otherwise we fall through to a fresh read rather than
     * hand back a partial structure (NaN/""/0 metadata). Metadata getters pass
     * useMonitorCache=false and always read fresh. */
    if (useMonitorCache) {
        std::map<std::string, MonEntry>::iterator it = g_monitors.find(name);
        if (it != g_monitors.end() && it->second.latest &&
            (!requireWholeMonitor || monitorCovers(it->second.request, request)))
            return it->second.latest;
    }

    try {
        PvaClientChannelPtr ch = client()->channel(name, g_provider, g_timeout);
        PvaClientGetPtr g = ch->get(request);
        g->get();
        return deepCopy(g->getData()->getPVStructure());
    } catch (std::exception &e) {
        err.err = PVA_NOTCONNECTED;
        err.msg = std::string("pvaGet '") + name + "': " + e.what();
        pvaSetLastError(err.err, err.msg);
        return PVStructurePtr();
    }
}

PvaClientPutPtr pvaPutPrepare(const std::string &name, const std::string &request, PvaError &err)
{
    try {
        PvaClientChannelPtr ch = client()->channel(name, g_provider, g_timeout);
        return ch->put(request);          /* connects and fetches current value */
    } catch (std::exception &e) {
        err.err = PVA_NOTCONNECTED;
        err.msg = std::string("pvaPut '") + name + "': " + e.what();
        pvaSetLastError(err.err, err.msg);
        return PvaClientPutPtr();
    }
}

void pvaPutCommit(const PvaClientPutPtr &put, std::size_t changedFieldOffset,
                  bool wait, PvaError &err)
{
    if (!put) { err.err = PVA_INTERNAL; err.msg = "null put"; return; }
    try {
        /* Mark what changed so pvAccess sends it. Offset 0 == the whole
         * structure (used by pvaPutStructure); a specific offset is used by
         * pvaPut, which only touched one field. */
        BitSetPtr changed = put->getData()->getChangedBitSet();
        changed->set((uint32)changedFieldOffset);

        if (wait) {
            put->put();                   /* issuePut + waitPut */
        } else {
            put->issuePut();              /* fire and forget    */
        }
    } catch (std::exception &e) {
        err.err = PVA_FAILURE;
        err.msg = std::string("pvaPut commit: ") + e.what();
        pvaSetLastError(err.err, err.msg);
    }
}

/* ---- monitor registry operations ------------------------------------ */

void pvaMonitorSet(const std::string &name, const std::string &request, PvaError &err)
{
    try {
        PvaClientChannelPtr ch = client()->channel(name, g_provider, g_timeout);
        /* Tear down any existing monitor on this name before re-subscribing,
         * so we don't leave the previous server-side subscription running. */
        std::map<std::string, MonEntry>::iterator old = g_monitors.find(name);
        if (old != g_monitors.end()) {
            if (old->second.mon) { try { old->second.mon->stop(); } catch (...) {} }
            g_monitors.erase(old);
        }
        MonEntry e;
        e.mon = ch->monitor(request);     /* creates, connects, starts */
        e.request = request;              /* remembered so cache-served structure
                                             reads can check field coverage */
        g_monitors[name] = e;
    } catch (std::exception &ex) {
        err.err = PVA_NOTCONNECTED;
        err.msg = std::string("pvaSetMonitor '") + name + "': " + ex.what();
        pvaSetLastError(err.err, err.msg);
    }
}

bool pvaMonitorPoll(const std::string &name, PvaError &err)
{
    std::map<std::string, MonEntry>::iterator it = g_monitors.find(name);
    if (it == g_monitors.end()) {
        err.err = PVA_NOMONITOR;
        err.msg = "no monitor set on '" + name + "'";
        return false;
    }
    MonEntry &e = it->second;
    bool got = false;
    try {
        while (e.mon->poll()) {           /* drain queue, keep the last */
            e.latest = deepCopy(e.mon->getData()->getPVStructure());
            e.mon->releaseEvent();
            got = true;
        }
    } catch (std::exception &ex) {
        err.err = PVA_FAILURE;
        err.msg = std::string("pvaNewMonitorValue '") + name + "': " + ex.what();
        pvaSetLastError(err.err, err.msg);
        return false;
    }
    if (got) e.unseen = true;
    bool r = e.unseen;
    e.unseen = false;
    return r;
}

bool pvaMonitorWait(const std::string &name, double timeout, PvaError &err)
{
    std::map<std::string, MonEntry>::iterator it = g_monitors.find(name);
    if (it == g_monitors.end()) {
        err.err = PVA_NOMONITOR;
        err.msg = "no monitor set on '" + name + "'";
        return false;
    }
    MonEntry &e = it->second;
    double t = timeout > 0 ? timeout : g_timeout;
    try {
        if (!e.mon->waitEvent(t)) return false;   /* timed out */
        e.latest = deepCopy(e.mon->getData()->getPVStructure());
        e.mon->releaseEvent();
        e.unseen = false;                          /* consumed by the wait */
        return true;
    } catch (std::exception &ex) {
        err.err = PVA_FAILURE;
        err.msg = std::string("pvaNewMonitorWait '") + name + "': " + ex.what();
        pvaSetLastError(err.err, err.msg);
        return false;
    }
}

PVStructurePtr pvaMonitorLatest(const std::string &name)
{
    std::map<std::string, MonEntry>::iterator it = g_monitors.find(name);
    return it == g_monitors.end() ? PVStructurePtr() : it->second.latest;
}

bool pvaMonitorActive(const std::string &name)
{
    return g_monitors.find(name) != g_monitors.end();
}

void pvaClear(const std::string &name)
{
    if (name.empty()) {
        for (std::map<std::string, MonEntry>::iterator it = g_monitors.begin();
             it != g_monitors.end(); ++it) {
            if (it->second.mon) { try { it->second.mon->stop(); } catch (...) {} }
        }
        g_monitors.clear();
    } else {
        std::map<std::string, MonEntry>::iterator it = g_monitors.find(name);
        if (it != g_monitors.end()) {
            if (it->second.mon) { try { it->second.mon->stop(); } catch (...) {} }
            g_monitors.erase(it);
        }
    }
}

} // namespace labpva
