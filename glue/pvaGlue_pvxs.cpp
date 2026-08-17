/* pvaGlue_pvxs.cpp - see pvaGlue.h. PVXS-backend implementation.
 *
 * Same interface as pvaGlue_pvac.cpp (the classic pvaClient backend), built on
 * the PVXS client library instead: one process-wide pvxs::client::Context
 * created from the EPICS_PVA_* environment, a channel registry of
 * client::Connect handles (backs pvaChannels / pvaIsConnected), monitors via
 * client::Subscription with the same drain-keep-latest cache semantics, and
 * puts via the PutBuilder (the argument Value is pre-built on the MATLAB
 * thread -- see pvaConvert.h -- so no callback ever touches MATLAB memory).
 *
 * NOTE: PVXS implements pvAccess only -- there is no Channel Access provider,
 * so pvaSetProvider('ca') is refused (use labca for record.FIELD access).
 */

/* Opts in to PVXS's "expert" client API: Builder::autoExec(false) plus
 * Operation::reExecPut, which together give a warm (reusable) put operation
 * -- the analogue of the classic backend's cached pvaClient put handle. See
 * the warm put channel section below. Must precede any pvxs header (version.h
 * turns this into PVXS_EXPERT_API_ENABLED, which gates those declarations). */
#define PVXS_ENABLE_EXPERT_API

#include "pvaGlue.h"

#include <pvxs/client.h>
#include <pvxs/log.h>

#include <epicsEvent.h>
#include <epicsTime.h>

#include <cctype>
#include <functional>
#include <map>
#include <mutex>
#include <stdexcept>
#include <vector>

using namespace pvxs;

namespace labpva {

void backendGuardPvxs() {}   /* link guard -- see pvaGlue.h */

/* ---- configuration state -------------------------------------------- */

static double g_timeout = 5.0;
static bool   g_debug   = false;

bool pvaSetProvider(const std::string &p)
{
    /* Accept only the pvAccess token; refusing 'ca' here lets the (backend-
     * neutral) MEX raise a clear error instead of silently misbehaving. */
    return p.empty() || p == "pva";
}

std::string pvaGetProvider() { return "pva"; }

void   pvaSetTimeout(double s) { if (s > 0) g_timeout = s; }
double pvaGetTimeout()         { return g_timeout; }

void pvaSetDebug(bool on)
{
    g_debug = on;
    logger_level_set("pvxs.*", on ? Level::Debug : Level::Err);
}
bool pvaGetDebug() { return g_debug; }

/* ---- the single client context --------------------------------------- */

static client::Context &context()
{
    /* Created lazily from the EPICS_PVA_* environment on first use; lives for
     * the whole MATLAB session (the MEX are mexLock'd, so the library -- and
     * the context's worker threads -- stay mapped until process exit). */
    static bool once = false;
    if (!once) { logger_config_env(); once = true; }
    static client::Context ctxt(client::Context::fromEnv());
    return ctxt;
}

/* ---- pvRequest handling ----------------------------------------------- */

static std::string stripWs(const std::string &s)
{
    std::string o;
    o.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i)
        if (!std::isspace((unsigned char)s[i])) o.push_back(s[i]);
    return o;
}

/* Apply labpva's request convention to a pvxs builder. PVXS's pvRequest()
 * string parser does NOT treat "field(a,b,c)" as a field list (its documented
 * form is repeated field() clauses), and silently falls back to the whole
 * structure -- verified live. So translate the common "field(a,b,...)" shape
 * into the builder's .field() calls; anything more exotic is passed through
 * pvRequest() verbatim. Empty / "field()" means the whole structure. */
template<class Builder>
static void applyRequest(Builder &b, const std::string &request)
{
    std::string r = stripWs(request);
    if (r.empty() || r == "field()" || r == "field") return;
    if (r.compare(0, 6, "field(") == 0 && r[r.size() - 1] == ')' &&
        r.find('(', 6) == std::string::npos) {
        std::string list = r.substr(6, r.size() - 7);
        size_t pos = 0;
        while (pos < list.size()) {
            size_t c = list.find(',', pos);
            if (c == std::string::npos) c = list.size();
            if (c > pos) b.field(list.substr(pos, c - pos));
            pos = c + 1;
        }
    } else {
        b.pvRequest(request);              /* general pvRequest expression */
    }
}

/* Does a monitor subscribed with `monReq` carry every field a get for `getReq`
 * asks for? A whole-structure monitor (`field()` / empty) covers any request;
 * otherwise we only trust an exact (whitespace-insensitive) request match. */
static bool monitorCovers(const std::string &monReq, const std::string &getReq)
{
    std::string m = stripWs(monReq);
    if (m.empty() || m == "field()" || m == "field") return true;
    return m == stripWs(getReq);
}

/* ---- channel registry ------------------------------------------------ */

/* Every channel labpva touches is recorded as a client::Connect handle so
 * pvaChannels can list it and pvaIsConnected can report live state. (PVXS
 * also caches channels inside the context; Connect additionally pins the
 * channel open for the session, matching the pvac backend's behaviour.) */
static std::map<std::string, std::shared_ptr<client::Connect> > g_channels;

static void recordChannel(const std::string &name)
{
    if (g_channels.find(name) == g_channels.end())
        g_channels[name] = context().connect(name).exec();
}

std::vector<std::string> pvaChannelNames()
{
    std::vector<std::string> out;
    out.reserve(g_channels.size());
    for (std::map<std::string, std::shared_ptr<client::Connect> >::const_iterator
             it = g_channels.begin(); it != g_channels.end(); ++it)
        out.push_back(it->first);
    return out;
}

bool pvaChannelConnected(const std::string &name)
{
    std::map<std::string, std::shared_ptr<client::Connect> >::iterator it =
        g_channels.find(name);
    return it != g_channels.end() && it->second && it->second->connected();
}

/* ---- monitor registry ------------------------------------------------ */

struct MonEntry {
    std::shared_ptr<client::Subscription> sub;
    std::shared_ptr<epicsEvent>           evt;      /* signalled by the FIFO
                                                       not-empty callback     */
    PvValue                               latest;   /* most recent polled sample */
    std::string                           request;  /* subscription's pvRequest  */
};

static std::map<std::string, MonEntry> g_monitors;

void pvaMonitorSet(const std::string &name, const std::string &request, PvaError &err)
{
    try {
        /* Tear down any existing monitor on this name before re-subscribing,
         * so we don't leave the previous server-side subscription running. */
        std::map<std::string, MonEntry>::iterator old = g_monitors.find(name);
        if (old != g_monitors.end()) {
            if (old->second.sub) { try { old->second.sub->cancel(); } catch (...) {} }
            g_monitors.erase(old);
        }

        MonEntry e;
        e.request = request;
        e.evt.reset(new epicsEvent());
        std::shared_ptr<epicsEvent> evt(e.evt);

        client::MonitorBuilder b(context().monitor(name));
        applyRequest(b, request);
        /* Mask connection-state events so pop() yields data only; the event
         * callback runs on a PVXS worker thread and must therefore do nothing
         * but signal (it must never touch MATLAB memory). NOTE: subscribing is
         * asynchronous -- a nonexistent PV does not fail here; its poll/wait
         * simply never reports a sample. */
        b.maskConnected(true).maskDisconnected(true);
        b.event([evt](client::Subscription &) { evt->signal(); });
        e.sub = b.exec();

        g_monitors[name] = e;
        recordChannel(name);
    } catch (std::exception &ex) {
        err.err = PVA_NOTCONNECTED;
        err.msg = std::string("pvaSetMonitor '") + name + "': " + ex.what();
        pvaSetLastError(err.err, err.msg);
    }
}

/* Drain the subscription queue keeping the newest sample. */
static bool drainQueue(MonEntry &e)
{
    bool got = false;
    while (true) {
        Value v(e.sub->pop());
        if (!v) break;
        e.latest = v;
        got = true;
    }
    return got;
}

bool pvaMonitorPoll(const std::string &name, PvaError &err)
{
    std::map<std::string, MonEntry>::iterator it = g_monitors.find(name);
    if (it == g_monitors.end()) {
        err.err = PVA_NOMONITOR;
        err.msg = "no monitor set on '" + name + "'";
        return false;
    }
    try {
        return drainQueue(it->second);
    } catch (std::exception &ex) {         /* e.g. RemoteError */
        err.err = PVA_FAILURE;
        err.msg = std::string("pvaNewMonitorValue '") + name + "': " + ex.what();
        pvaSetLastError(err.err, err.msg);
        return false;
    }
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
        epicsTime deadline = epicsTime::getCurrent() + t;
        while (true) {
            if (drainQueue(e)) return true;
            double remaining = deadline - epicsTime::getCurrent();
            if (remaining <= 0) return false;                 /* timed out */
            if (!e.evt->wait(remaining)) return false;        /* timed out */
        }
    } catch (std::exception &ex) {
        err.err = PVA_FAILURE;
        err.msg = std::string("pvaNewMonitorWait '") + name + "': " + ex.what();
        pvaSetLastError(err.err, err.msg);
        return false;
    }
}

PvValue pvaMonitorLatest(const std::string &name)
{
    std::map<std::string, MonEntry>::iterator it = g_monitors.find(name);
    return it == g_monitors.end() ? PvValue() : it->second.latest;
}

bool pvaMonitorActive(const std::string &name)
{
    return g_monitors.find(name) != g_monitors.end();
}

std::vector<std::string> pvaMonitorNames()
{
    std::vector<std::string> out;
    out.reserve(g_monitors.size());
    for (std::map<std::string, MonEntry>::const_iterator it = g_monitors.begin();
         it != g_monitors.end(); ++it)
        out.push_back(it->first);
    return out;
}

/* ---- write ------------------------------------------------------------ */

/* A pvxs Operation is cancelled when its handle is dropped, so a fire-and-
 * forget put (pvaPutNoWait) must park its handle until completion. The result
 * callback (on a PVXS worker thread) only records the key of a finished put;
 * the handle itself is destroyed later, on the MATLAB thread, by reapDonePuts
 * -- never from inside its own callback. */
static std::mutex g_putLock;
static std::map<unsigned long long, std::shared_ptr<client::Operation> > g_pendingPuts;
static std::vector<unsigned long long> g_donePuts;
static unsigned long long g_putSeq = 0;

/* Bound on parked no-wait puts: a pvaPutNoWait to a PV that never answers
 * (nonexistent name, IOC down) never completes and would otherwise accumulate
 * live operations for the whole session. Beyond the cap the OLDEST pending put
 * is cancelled and dropped. */
static const size_t MAX_PENDING_PUTS = 256;

static void reapDonePuts()
{
    std::vector<std::shared_ptr<client::Operation> > graveyard;
    {
        std::lock_guard<std::mutex> G(g_putLock);
        std::vector<unsigned long long> keep;
        for (size_t i = 0; i < g_donePuts.size(); ++i) {
            std::map<unsigned long long,
                     std::shared_ptr<client::Operation> >::iterator it =
                g_pendingPuts.find(g_donePuts[i]);
            if (it != g_pendingPuts.end()) {
                graveyard.push_back(it->second);
                g_pendingPuts.erase(it);
            } else {
                /* completion callback fired before the handle was parked --
                 * keep the key for the next sweep */
                keep.push_back(g_donePuts[i]);
            }
        }
        g_donePuts.swap(keep);
    }
    /* graveyard destructs here, outside the lock, on the MATLAB thread; the
     * operations are already complete so destruction cancels nothing */
}

/* ---- warm put channels ------------------------------------------------
 *
 * A pvxs Operation is single-shot by default, so a put costs two exchanges
 * with the server -- the INIT that negotiates the type, then the data -- and
 * labpva used to read the channel first (two more) just to learn that type.
 * Four round trips per pvaPut, where the classic backend's cached pvaClient
 * put handle needs one. This restores the warm-handle behaviour: ONE
 * operation per channel, created with .autoExec(false) so it stops after the
 * INIT exchange, then fired repeatedly with Operation::reExecPut (PVXS's
 * expert API). Steady state is one round trip per put, and the INIT prototype
 * doubles as the type template the argument is built from -- no read at all.
 *
 * Two properties of the expert API shape everything below:
 *
 * (1) reExecPut acts only when the operation is Idle (INIT done, nothing in
 *     flight) and is SILENTLY IGNORED otherwise -- notably mid-reconnect, or
 *     while a previous fire-and-forget put is still in flight. So a warm put
 *     is always bounded by the labpva timeout, and the warm operation is used
 *     only when it is verifiably ready (alive + free + connected + matching
 *     type); in every other state the put runs on a one-shot operation, which
 *     handles those states natively.
 *
 * (2) A disconnect while a put is in Exec parks the operation at Done FOREVER
 *     ("can't restart as server side-effects may occur" -- pvxs clientget.cpp)
 *     without any reconnect reviving it. The completion callbacks detect that
 *     terminal state (ChanState.dead) so the channel is dropped and rebuilt
 *     instead of silently swallowing later puts.
 *
 * Both pvaPut and pvaPutNoWait use the warm operation when it is ready. A
 * failed or timed-out warm put is NEVER re-executed automatically: the ack
 * may merely be late while the put was already applied, and a put is not
 * idempotent (.PROC, relative moves) -- retrying is the caller's decision. */

/* ---- shared warm-operation machinery ---------------------------------
 *
 * Used by BOTH warm puts and warm reads (see the read section): the INIT
 * handshake state, the completion handshake for one execution, and the two
 * waits. Everything here is written by PVXS worker threads and read on the
 * MATLAB thread, hence the mutexes; none of it touches MATLAB memory. */

struct OpInit {
    std::mutex   lock;
    epicsEvent   ready;         /* signalled on a PVXS worker, either way     */
    Value        proto;         /* INIT prototype: type description, no data  */
    unsigned     generation;    /* bumped per INIT, incl. after a reconnect   */
    std::string  failure;       /* INIT rejected by the server (else empty)   */
    OpInit() : generation(0) {}
};

/* Records the INIT prototype and wakes the waiter. */
static std::function<void(const Value &)> onInitCb(const std::shared_ptr<OpInit> &init)
{
    return [init](const Value &proto) {
        {
            std::lock_guard<std::mutex> G(init->lock);
            init->proto = proto;
            ++init->generation;
        }
        init->ready.signal();
    };
}

/* With autoExec(false) an operation's result callback fires only if the INIT
 * itself fails -- a successful INIT just parks at Idle -- so this reports a
 * server that refuses the operation instead of leaving the wait below to time
 * out and blame the connection. (Once running, reExecGet/reExecPut install
 * their own per-execution callback in its place.) */
static std::function<void(client::Result &&)> onInitFailCb(const std::shared_ptr<OpInit> &init)
{
    return [init](client::Result &&result) {
        std::string msg("refused by server");
        try { result(); } catch (std::exception &e) { msg = e.what(); }
        {
            std::lock_guard<std::mutex> G(init->lock);
            init->failure = msg;
        }
        init->ready.signal();
    };
}

/* Block until the operation's INIT exchange completes. False with `err` set if
 * the server refused it (failCode) or it did not answer in time (tmoCode). */
static bool awaitOpInit(const std::shared_ptr<OpInit> &init, const std::string &prefix,
                        int failCode, int tmoCode, PvaError &err)
{
    epicsTime deadline = epicsTime::getCurrent() + g_timeout;
    while (true) {
        {
            std::lock_guard<std::mutex> G(init->lock);
            if (init->generation) return true;        /* INIT reply arrived */
            if (!init->failure.empty()) {
                err.err = failCode;
                err.msg = prefix + init->failure;
                pvaSetLastError(err.err, err.msg);
                return false;
            }
        }
        double remaining = deadline - epicsTime::getCurrent();
        if (remaining <= 0) {
            err.err = tmoCode;
            err.msg = prefix + "channel did not connect";
            pvaSetLastError(err.err, err.msg);
            return false;
        }
        init->ready.wait(remaining);
    }
}

/* Per-channel health, shared with the PVXS worker threads that run the
 * completion callbacks. `busy` is set while a fire-and-forget put is in
 * flight on the warm operation (pvxs silently ignores a second reExecPut
 * until the first completes, so the next put must know). `dead` records that
 * a callback saw the operation reach its terminal state -- see opKilledBy()
 * -- after which every reExec on it is silently ignored forever, so the
 * channel must be dropped and rebuilt, never reused. */
struct ChanState {
    std::mutex lock;
    bool       busy;
    epicsTime  busyDeadline;    /* past this, treat a set flag as never-clearing */
    bool       dead;
    /* One-shot no-wait puts still pending on this channel. Their data goes on
     * the wire only after their own INIT round trip, so a warm reExec issued
     * meanwhile -- whose data is sent immediately -- would OVERTAKE them and
     * the channel's last-written value would not be the last value issued.
     * While this is nonzero (and not past its deadline: an evicted/abandoned
     * one-shot never runs its callback), the warm operation must not fire. */
    unsigned   pendingOneShot;
    epicsTime  pendingDeadline;
    ChanState() : busy(false), dead(false), pendingOneShot(0) {}
};

/* Does this failed Result mean the operation is permanently finished?
 * A RemoteError (the server rejected the exec) returns a !autoExec operation
 * to Idle -- reusable. A Disconnect while a PUT is in Exec parks it at Done
 * ("can't restart as server side-effects may occur", pvxs clientget.cpp), and
 * anything unrecognised is treated the same way: wrongly dropping a live
 * channel costs one INIT; wrongly keeping a dead one costs a silent no-op. */
static bool opKilledBy(client::Result &result, std::string &msg)
{
    try { result(); msg.clear(); return false; }
    catch (client::RemoteError &e) { msg = e.what(); return false; }
    catch (std::exception &e)      { msg = e.what(); return true; }
}

/* Completion state for one execution of a warm operation. One instance lives
 * in each warm channel and is re-armed per execution: `gen` stamps the
 * execution, and a callback whose stamp no longer matches (it fired after its
 * execution timed out and the channel moved on) is ignored rather than being
 * mistaken for the current execution's result. */
struct OpSync {
    std::mutex  lock;
    epicsEvent  evt;
    unsigned    gen;
    bool        done;
    bool        ok;
    std::string msg;
    Value       data;           /* the reply value (reads only) */
    OpSync() : gen(0), done(false), ok(false) {}
};

/* Arm for a new execution; returns the stamp its callback must carry. */
static unsigned opSyncArm(OpSync &s)
{
    std::lock_guard<std::mutex> G(s.lock);
    ++s.gen;
    s.done = false;
    s.ok   = false;
    s.msg.clear();
    s.data = Value();
    return s.gen;
}

/* The per-execution result callback handed to reExecGet / reExecPut. When a
 * ChanState is given (put channels), a fatal failure also marks it dead. */
static std::function<void(client::Result &&)> onDoneCb(const std::shared_ptr<OpSync> &sync,
                                                       unsigned gen,
                                                       const std::shared_ptr<ChanState> &st =
                                                           std::shared_ptr<ChanState>())
{
    return [sync, gen, st](client::Result &&result) {
        bool ok = true, killed = false;
        std::string msg;
        Value data;
        try { data = result(); }
        catch (client::RemoteError &e) { ok = false; msg = e.what(); }
        catch (std::exception &e)      { ok = false; msg = e.what(); killed = true; }
        if (st && killed) {
            std::lock_guard<std::mutex> G(st->lock);
            st->dead = true;
        }
        {
            std::lock_guard<std::mutex> G(sync->lock);
            if (sync->gen != gen) return;    /* stale execution: ignore */
            sync->ok   = ok;
            sync->msg  = msg;
            sync->data = data;
            sync->done = true;
        }
        sync->evt.signal();
    };
}

/* Block for that execution to complete. False with `err` set on failure;
 * `retry` asks the CALLER to drop this warm channel and rebuild it (the caller
 * owns the registry, so its entry stays valid in here). */
static bool awaitOpSync(const std::shared_ptr<OpSync> &sync, const std::string &prefix,
                        int failCode, int tmoCode, PvaError &err, bool &retry)
{
    retry = false;
    epicsTime deadline = epicsTime::getCurrent() + g_timeout;
    while (true) {
        {
            std::lock_guard<std::mutex> G(sync->lock);
            if (sync->done) {
                if (sync->ok) return true;
                err.err = failCode;
                err.msg = prefix + sync->msg;
                pvaSetLastError(err.err, err.msg);
                return false;
            }
        }
        double remaining = deadline - epicsTime::getCurrent();
        if (remaining <= 0) {
            /* Either the server never answered, or the operation was not Idle
             * and reExec silently did nothing (a reconnect in progress). Ask
             * the caller to rebuild -- the callback holds its own reference to
             * `sync` and its generation stamp no longer matches once the sync
             * is re-armed, so completing late is harmless. */
            retry = true;
            err.err = tmoCode;
            err.msg = prefix + "timed out";
            pvaSetLastError(err.err, err.msg);
            return false;
        }
        sync->evt.wait(remaining);
    }
}

/* Both warm-channel registries (puts below, reads in the read section) are
 * LRU-bounded: each cached entry pins one server-side operation, so beyond
 * the cap the least recently used entry is dropped -- and transparently
 * rebuilt if that channel is used again. One clock serves both. */
static unsigned long long g_chanUseSeq = 0;

template<class Map>
static typename Map::iterator lruEntry(Map &m)
{
    typename Map::iterator lru = m.begin();
    for (typename Map::iterator i = m.begin(); i != m.end(); ++i)
        if (i->second.lastUse < lru->second.lastUse) lru = i;
    return lru;
}

/* ---- warm put channels ----------------------------------------------- */

struct PutChan {
    std::shared_ptr<client::Operation> op;        /* Idle between puts        */
    std::shared_ptr<OpInit>            init;
    std::shared_ptr<ChanState>         st;
    std::shared_ptr<OpSync>            sync;      /* re-armed per execution   */
    unsigned long long                 lastUse;
    PutChan() : lastUse(0) {}
};

static const size_t MAX_PUT_CHANNELS = 256;

static std::map<std::string, PutChan> g_putChans;

/* Operations retired while a fire-and-forget put was still in flight on them:
 * destroying such an operation would CANCEL the put after pvaPutNoWait already
 * reported it issued, so dropPutChan parks the handle here instead and
 * reapRetiredPuts destroys it -- on the MATLAB thread -- once its completion
 * callback has cleared `busy` (or its deadline has passed, i.e. the callback
 * will never run). */
static std::vector<std::pair<std::shared_ptr<client::Operation>,
                             std::shared_ptr<ChanState> > > g_retiredPuts;

static void reapRetiredPuts()
{
    std::vector<std::shared_ptr<client::Operation> > graveyard;
    for (size_t i = 0; i < g_retiredPuts.size(); ) {
        bool settled;
        {
            std::lock_guard<std::mutex> G(g_retiredPuts[i].second->lock);
            settled = !g_retiredPuts[i].second->busy ||
                      epicsTime::getCurrent() > g_retiredPuts[i].second->busyDeadline;
        }
        if (settled) {
            graveyard.push_back(g_retiredPuts[i].first);
            g_retiredPuts.erase(g_retiredPuts.begin() + i);
        } else {
            ++i;
        }
    }
    /* graveyard destructs here, outside any lock; the operations are complete
     * (or abandoned past their deadline), so destruction cancels no live put */
}

/* One reap for everything a finished/abandoned put leaves behind, called once
 * per put (pvaPutExec) and on pvaClear. */
static void reapFinishedPuts()
{
    reapDonePuts();
    reapRetiredPuts();
}

/* Remove a channel's warm put operation; the next put to that name builds a
 * fresh one. An operation with a fire-and-forget put still in flight is
 * retired (see above) rather than destroyed, so the put is not cancelled. */
static void dropPutChan(const std::string &name)
{
    std::map<std::string, PutChan>::iterator it = g_putChans.find(name);
    if (it == g_putChans.end()) return;
    bool inflight;
    {
        std::lock_guard<std::mutex> G(it->second.st->lock);
        inflight = it->second.st->busy &&
                   epicsTime::getCurrent() <= it->second.st->busyDeadline;
    }
    if (inflight)
        g_retiredPuts.push_back(std::make_pair(it->second.op, it->second.st));
    g_putChans.erase(it);
}

/* The channel's current INIT prototype (written by a worker thread). */
static Value putChanInitProto(PutChan &c)
{
    std::lock_guard<std::mutex> G(c.init->lock);
    return c.init->proto;
}

/* Is the channel's warm operation dead (terminal state observed) or its
 * (re-)INIT refused? Either way it must be dropped, not reused. */
static bool putChanDead(PutChan &c)
{
    {
        std::lock_guard<std::mutex> G(c.st->lock);
        if (c.st->dead) return true;
    }
    std::lock_guard<std::mutex> G(c.init->lock);
    return !c.init->failure.empty();
}

/* Is the channel's warm operation free to fire right now? Not while a warm
 * no-wait put is in flight (pvxs would silently swallow the reExec) and not
 * while one-shot no-wait puts are pending (the warm data would overtake
 * theirs -- see ChanState). `stale` reports a busy flag that has outlived the
 * timeout -- a no-wait put issued into a reconnect, which reExecPut silently
 * ignored, so its completion callback will never run and release the channel;
 * the caller rebuilds instead. */
static bool putChanFree(PutChan &c, bool &stale)
{
    stale = false;
    std::lock_guard<std::mutex> G(c.st->lock);
    if (c.st->busy) {
        if (epicsTime::getCurrent() > c.st->busyDeadline) stale = true;
        return false;
    }
    if (c.st->pendingOneShot &&
        epicsTime::getCurrent() <= c.st->pendingDeadline)
        return false;                        /* ordering barrier */
    return true;
}

/* The channel's warm put operation, INIT complete. NULL with `err` set if it
 * could not be created or did not INIT within the timeout. A cached entry
 * that died (disconnect during a put, refused re-INIT) is rebuilt here. */
static PutChan *openPutChan(const std::string &name, PvaError &err)
{
    std::map<std::string, PutChan>::iterator it = g_putChans.find(name);
    if (it != g_putChans.end()) {
        if (!putChanDead(it->second)) {
            it->second.lastUse = ++g_chanUseSeq;
            return &it->second;
        }
        dropPutChan(name);                   /* dead: rebuild below */
    }

    PutChan c;
    c.init.reset(new OpInit());
    c.st.reset(new ChanState());
    c.sync.reset(new OpSync());
    std::shared_ptr<OpInit> init(c.init);

    /* fetchPresent(false): the argument is pre-built on the MATLAB thread
     * (pvaConvert.h mxToPutArg*), so the server need not send the current
     * value first. The build callback (a PVXS worker thread) is replaced by
     * reExecPut anyway; it only has to exist. */
    c.op = context().put(name)
                    .fetchPresent(false)
                    .autoExec(false)
                    .onInit(onInitCb(init))
                    .result(onInitFailCb(init))
                    .build([](Value &&prototype) -> Value {
                        return std::move(prototype);
                    })
                    .exec();

    /* c.op destructs on failure here, cancelling the pending operation */
    if (!awaitOpInit(init, "pvaPut '" + name + "': ",
                     PVA_FAILURE, PVA_NOTCONNECTED, err))
        return NULL;

    if (g_putChans.size() >= MAX_PUT_CHANNELS)
        dropPutChan(lruEntry(g_putChans)->first);   /* retires, not cancels */

    c.lastUse = ++g_chanUseSeq;
    return &(g_putChans[name] = c);
}

/* Does this type describe an enum PV -- the one case where the put argument
 * needs DATA (the choice list) that an INIT prototype cannot carry? */
static bool isEnumValue(const Value &proto)
{
    Value v = proto["value"];
    if (!v || v.type().code != TypeCode::Struct) return false;
    try { return v.id() == "enum_t"; } catch (std::exception &) { return false; }
}

PvValue pvaPutProto(const std::string &name, PvaError &err)
{
    try {
        PutChan *c = openPutChan(name, err);
        if (!c) return PvValue();

        Value initProto(putChanInitProto(*c));
        if (isEnumValue(initProto)) {
            /* An enum's choices are DATA the INIT prototype cannot carry --
             * and they can change server-side without a reconnect (dbpf on
             * the choice strings, autosave restore), which would make a
             * cached list write a WRONG INDEX with no error. So read them
             * fresh for every enum put; the read goes over the warm read
             * channel, one round trip. */
            PvValue cur = pvaGet(name, "field()", err);
            if (err.err != PVA_OK) return PvValue();
            recordChannel(name);
            return cur;
        }
        recordChannel(name);
        return initProto;
    } catch (std::exception &e) {
        err.err = PVA_FAILURE;
        err.msg = std::string("pvaPut '") + name + "': " + e.what();
        pvaSetLastError(err.err, err.msg);
        return PvValue();
    }
}

/* Fire the channel's warm operation and block for completion. Returns false
 * with `err` set on failure; `retry` reports a timeout, after which the CALLER
 * must drop this channel (the operation may be mid-reconnect or dead) and
 * must NOT re-execute the argument -- the put may already have been applied
 * server-side, and a put is not idempotent. */
static bool warmPutExec(const std::string &name, PutChan &c, const PvValue &arg,
                        PvaError &err, bool &retry)
{
    unsigned gen = opSyncArm(*c.sync);
    c.op->reExecPut(arg, onDoneCb(c.sync, gen, c.st));
    return awaitOpSync(c.sync, "pvaPut '" + name + "': ",
                       PVA_FAILURE, PVA_TIMEOUT, err, retry);
}

/* Fire a fire-and-forget put on the warm operation. Failures are not reported
 * (same as the one-shot no-wait path), but the completion callback releases
 * the channel for the next put and marks it dead if the operation reached its
 * terminal state (disconnect during the put) -- otherwise the NEXT no-wait
 * put would be silently swallowed by the dead operation while still being
 * reported as issued. False => caller must fall back to a one-shot put. */
static bool warmPutNoWait(PutChan &c, const PvValue &arg)
{
    std::shared_ptr<ChanState> st(c.st);
    {
        std::lock_guard<std::mutex> G(st->lock);
        st->busy         = true;
        st->busyDeadline = epicsTime::getCurrent() + g_timeout;
    }
    try {
        c.op->reExecPut(arg, [st](client::Result &&result) {
            std::string msg;
            bool killed = opKilledBy(result, msg);
            std::lock_guard<std::mutex> G(st->lock);
            st->busy = false;
            if (killed) st->dead = true;
        });
        return true;
    } catch (std::exception &) {
        std::lock_guard<std::mutex> G(st->lock);
        st->busy = false;
        return false;
    }
}

/* The one-shot put's build callback (runs on a PVXS worker thread; must not
 * touch MATLAB memory). It validates the pre-built argument against the type
 * the operation just negotiated with the server: pvxs serialises the argument
 * against the argument's OWN descriptor, so a stale-typed argument (the IOC
 * was rebuilt with a different structure) would otherwise go onto the wire
 * and be misread by the server. Throwing here surfaces as a clean put error. */
static std::function<Value(Value &&)> oneShotBuilder(const PvValue &argCopy)
{
    return [argCopy](Value &&prototype) -> Value {
        if (prototype && !argCopy.equalType(prototype))
            throw std::runtime_error(
                "channel type changed since the put argument was built "
                "(server rebuilt?); retry the put");
        return argCopy;
    };
}

void pvaPutExec(const std::string &name, const PvValue &arg, bool wait, PvaError &err)
{
    reapFinishedPuts();
    try {
        PvValue argCopy(arg);

        /* The argument was pre-built on the MATLAB thread from the channel's
         * type template (pvaConvert.h mxToPutArg*), and only its MARKED fields
         * are sent; the server keeps the rest.
         *
         * The warm operation was opened by pvaPutProto just above in the same
         * MEX call, so this is a lookup, not a connect. It is usable only when
         * ALL of these hold; otherwise this put goes through a one-shot
         * operation, which handles every awkward state natively (it parks
         * until the channel connects, INITs its own type, and validates the
         * argument against it -- see oneShotBuilder):
         *   - it is not DEAD (a disconnect during a put parks the operation
         *     at Done forever; reExec on it is silently ignored),
         *   - no fire-and-forget put is in flight on it (same silent-ignore
         *     rule) -- a stale busy flag means dead in practice,
         *   - the channel is connected (an Idle operation survives a
         *     reconnect, but a reExec issued MID-reconnect is swallowed),
         *   - the argument's type matches the operation's negotiated type
         *     (reExecPut serialises the argument against its own descriptor
         *     with no server-side renegotiation). */
        std::map<std::string, PutChan>::iterator it = g_putChans.find(name);
        bool warmOk = false;
        if (it != g_putChans.end()) {
            bool stale = false;
            bool opFree = putChanFree(it->second, stale);
            if (putChanDead(it->second) || stale) {
                dropPutChan(name);
                it = g_putChans.end();
            } else {
                warmOk = opFree && pvaChannelConnected(name) &&
                         argCopy.equalType(putChanInitProto(it->second));
            }
        }

        if (wait) {
            if (warmOk) {
                bool retry = false;
                if (warmPutExec(name, it->second, argCopy, err, retry)) {
                    recordChannel(name);
                    return;
                }
                /* NO re-execution on failure: on a timeout or a mid-put
                 * disconnect the put may already have been APPLIED (the ack
                 * was merely lost), and a put is not idempotent -- silently
                 * firing the same argument again could double-execute a
                 * side-effect record (.PROC, a relative move). Drop the
                 * channel where it can no longer be trusted and report
                 * honestly; retrying is the caller's decision. */
                if (retry || putChanDead(it->second)) dropPutChan(name);
                return;
            }
            /* One-shot: fetchPresent(false) because the argument is already
             * built; oneShotBuilder returns it after checking its type (it
             * runs on a PVXS worker thread and never touches MATLAB memory). */
            std::shared_ptr<client::Operation> op(
                context().put(name)
                         .fetchPresent(false)
                         .build(oneShotBuilder(argCopy))
                         .exec());
            try {
                op->wait(g_timeout);       /* throws on error / timeout */
            } catch (client::Timeout &) {
                err.err = PVA_TIMEOUT;     /* same code as the warm path */
                err.msg = "pvaPut '" + name + "': timed out";
                pvaSetLastError(err.err, err.msg);
                return;
            }
        } else {
            /* Fire-and-forget on the warm operation when it is usable.
             * Otherwise a one-shot operation: it is never cancelled by a
             * dead/busy warm handle and pvxs re-issues it across a reconnect. */
            if (warmOk && warmPutNoWait(it->second, argCopy)) {
                recordChannel(name);
                return;
            }
            unsigned long long key;
            {
                std::lock_guard<std::mutex> G(g_putLock);
                key = ++g_putSeq;
            }
            /* Raise the channel's ordering barrier BEFORE issuing: until this
             * one-shot completes, the warm operation must not fire (its data
             * would overtake this put's -- see ChanState.pendingOneShot). */
            std::shared_ptr<ChanState> st;
            if (it != g_putChans.end()) {
                st = it->second.st;
                std::lock_guard<std::mutex> G(st->lock);
                ++st->pendingOneShot;
                st->pendingDeadline = epicsTime::getCurrent() + g_timeout;
            }
            std::shared_ptr<client::Operation> op(
                context().put(name)
                         .fetchPresent(false)
                         .build(oneShotBuilder(argCopy))
                         .result([key, st](client::Result &&) {
                             if (st) {
                                 std::lock_guard<std::mutex> G(st->lock);
                                 if (st->pendingOneShot) --st->pendingOneShot;
                             }
                             std::lock_guard<std::mutex> G(g_putLock);
                             g_donePuts.push_back(key);
                         })
                         .exec());
            std::shared_ptr<client::Operation> evicted;
            {
                std::lock_guard<std::mutex> G(g_putLock);
                if (g_pendingPuts.size() >= MAX_PENDING_PUTS) {
                    evicted = g_pendingPuts.begin()->second;
                    g_pendingPuts.erase(g_pendingPuts.begin());
                }
                g_pendingPuts[key] = op;
            }
            /* evicted (if any) destructs here, outside the lock, on the MATLAB
             * thread -- cancelling the stalest never-completed put */
        }
        recordChannel(name);
    } catch (std::exception &e) {
        err.err = PVA_FAILURE;
        err.msg = std::string("pvaPut '") + name + "': " + e.what();
        pvaSetLastError(err.err, err.msg);
    }
}

/* ---- clear ------------------------------------------------------------ */

/* Defined with the warm read channels further down (the read section owns that
 * registry); declared here so pvaClear can retire them too. */
static void dropGetChans(const std::string &name);
static void dropAllGetChans();

void pvaClear(const std::string &name)
{
    reapFinishedPuts();
    if (name.empty()) {
        for (std::map<std::string, MonEntry>::iterator it = g_monitors.begin();
             it != g_monitors.end(); ++it) {
            if (it->second.sub) { try { it->second.sub->cancel(); } catch (...) {} }
        }
        g_monitors.clear();
        /* Clear-everything also retires the cached put and read operations
         * (the connections themselves stay open, in g_channels); each is
         * rebuilt, with a fresh type template, on the next use of that
         * channel. dropPutChan parks -- rather than cancels -- an operation
         * with a fire-and-forget put still in flight. */
        while (!g_putChans.empty())
            dropPutChan(g_putChans.begin()->first);
        dropAllGetChans();
    } else {
        std::map<std::string, MonEntry>::iterator it = g_monitors.find(name);
        if (it != g_monitors.end()) {
            if (it->second.sub) { try { it->second.sub->cancel(); } catch (...) {} }
            g_monitors.erase(it);
        }
        dropPutChan(name);
        dropGetChans(name);
    }
}

/* ---- read ------------------------------------------------------------ */

/* Does this cached sample carry a plain (scalar / scalar-array / enum) `value`
 * -- i.e. exactly what smart-pvaGet's bare-value fast path returns? For a rich
 * PV (NTNDArray/NTTable/custom group) or a monitor whose request excluded
 * `value`, this is false and pvaGet must read fresh: serving the cache would
 * return a PARTIAL structure (e.g. an image missing dimension/codec). */
/* ---- warm read channels -----------------------------------------------
 *
 * The same story as warm put channels, one level up: a fresh pvxs get
 * Operation spends an INIT exchange negotiating the type before the exchange
 * that carries the data, where the classic backend's pvaClient get cache
 * (keyed by pvRequest, like this one) answers a warm read in one. So keep one
 * .autoExec(false) get Operation per (channel, pvRequest) and re-issue it with
 * Operation::reExecGet.
 *
 * A second benefit: a reused operation keeps its server-side and client-side
 * value cache, so repeat replies carry only the CHANGED fields and pvxs
 * refills the rest from its prototype (`cache_sync` in pvxs/src/data.cpp) --
 * cheaper in bytes as well as round trips. The delivered Value is complete
 * either way, and labpva's marshaller reads values, never marks, so a
 * partially-marked reply still converts in full. The flip side is memory: each
 * warm read channel retains one reply's worth of data (an image PV read
 * repeatedly holds a frame per request), which the LRU bound below and
 * pvaClear keep in check. */

struct GetChan {
    std::shared_ptr<client::Operation> op;        /* Idle between reads       */
    std::shared_ptr<OpInit>            init;
    std::shared_ptr<OpSync>            sync;      /* re-armed per execution   */
    unsigned long long                 lastUse;
    GetChan() : lastUse(0) {}
};

static const size_t MAX_GET_CHANNELS = 256;

/* Keyed by channel AND request: different requests are different operations
 * (that is also how pvaClient's get cache is keyed), so the value verb and the
 * metadata getters each keep their own. */
static std::map<std::string, GetChan> g_getChans;

static std::string getChanKey(const std::string &name, const std::string &request)
{
    return name + "\n" + stripWs(request);
}

/* The warm get operation for (name, request), INIT complete. NULL with `err`
 * set if it could not be created or did not INIT within the timeout. */
static GetChan *openGetChan(const std::string &name, const std::string &request,
                            PvaError &err)
{
    std::string key = getChanKey(name, request);
    std::map<std::string, GetChan>::iterator it = g_getChans.find(key);
    if (it != g_getChans.end()) {
        it->second.lastUse = ++g_chanUseSeq;
        return &it->second;
    }

    GetChan c;
    c.init.reset(new OpInit());
    c.sync.reset(new OpSync());
    std::shared_ptr<OpInit> init(c.init);

    client::GetBuilder b(context().get(name));
    applyRequest(b, request);
    b.autoExec(false).onInit(onInitCb(init)).result(onInitFailCb(init));
    c.op = b.exec();

    /* c.op destructs on failure here, cancelling the pending operation.
     * PVA_NOTCONNECTED for both cases: that is what a failed pvaGet has always
     * reported, so MATLAB error identifiers do not change. */
    if (!awaitOpInit(init, "pvaGet '" + name + "': ",
                     PVA_NOTCONNECTED, PVA_NOTCONNECTED, err))
        return NULL;

    if (g_getChans.size() >= MAX_GET_CHANNELS)
        g_getChans.erase(lruEntry(g_getChans));   /* reads cancel safely */

    c.lastUse = ++g_chanUseSeq;
    return &(g_getChans[key] = c);
}

static void dropGetChan(const std::string &name, const std::string &request)
{
    g_getChans.erase(getChanKey(name, request));
}

/* Every warm read operation on this channel, whatever its request (declared
 * above for pvaClear). */
static void dropGetChans(const std::string &name)
{
    std::string prefix = name + "\n";
    for (std::map<std::string, GetChan>::iterator it = g_getChans.begin();
         it != g_getChans.end(); ) {
        if (it->first.compare(0, prefix.size(), prefix) == 0)
            g_getChans.erase(it++);          /* its operation is cancelled here */
        else
            ++it;
    }
}

static void dropAllGetChans()
{
    g_getChans.clear();
}

/* Re-issue the warm read and block for the reply. False with `err` set on
 * failure; `retry` asks the CALLER to drop this channel and rebuild it. */
static bool warmGetExec(const std::string &name, GetChan &c, PvValue &out,
                        PvaError &err, bool &retry)
{
    unsigned gen = opSyncArm(*c.sync);
    c.op->reExecGet(onDoneCb(c.sync, gen));
    if (!awaitOpSync(c.sync, "pvaGet '" + name + "': ",
                     PVA_NOTCONNECTED, PVA_NOTCONNECTED, err, retry))
        return false;
    out = c.sync->data;
    return true;
}

static bool cachedValueIsPlain(const Value &latest)
{
    Value v = latest["value"];
    if (!v) return false;
    TypeCode tc = v.type();
    if (tc.code == TypeCode::Struct) {
        try { return v.id() == "enum_t"; } catch (std::exception &) { return false; }
    }
    return tc.code != TypeCode::StructA && tc.code != TypeCode::Union &&
           tc.code != TypeCode::UnionA  && tc.code != TypeCode::Any &&
           tc.code != TypeCode::AnyA;
}

PvValue pvaGet(const std::string &name, const std::string &request, PvaError &err,
               bool useMonitorCache, bool requireWholeMonitor)
{
    /* Same cache contract as the pvac backend: an active, polled monitor
     * serves the value read (and covered structure reads) with no round-trip.
     * The value verb (requireWholeMonitor=false) additionally requires the
     * cached sample to carry a plain `value` -- otherwise smart-pvaGet would
     * hand back the whole (possibly partial) monitored subset. */
    if (useMonitorCache) {
        std::map<std::string, MonEntry>::iterator it = g_monitors.find(name);
        if (it != g_monitors.end() && it->second.latest &&
            (requireWholeMonitor
                 ? monitorCovers(it->second.request, request)
                 : (monitorCovers(it->second.request, request) ||
                    cachedValueIsPlain(it->second.latest))))
            return it->second.latest;
    }

    try {
        /* A channel labpva knows to be disconnected right now: a reExecGet on
         * the warm operation would be silently ignored (not Idle) and only
         * time out, and rebuilding would burn a second timeout. A one-shot get
         * matches the old behaviour exactly -- it waits ONE timeout for the
         * channel to (re)connect and read. The warm entry, if any, stays; an
         * Idle get operation survives a reconnect and is reused next call. */
        if (g_channels.count(name) && !pvaChannelConnected(name)) {
            client::GetBuilder b(context().get(name));
            applyRequest(b, request);
            Value v(b.exec()->wait(g_timeout));   /* throws on error/timeout */
            recordChannel(name);
            return v;
        }

        /* Warm read: one exchange with the server, no per-call operation. */
        GetChan *c = openGetChan(name, request, err);
        if (!c) return PvValue();

        PvValue v;
        bool retry = false;
        if (warmGetExec(name, *c, v, err, retry)) {
            recordChannel(name);
            return v;
        }
        if (!retry) return PvValue();       /* the server refused the read */

        /* The read timed out. Unlike a put, a read is idempotent, so retrying
         * is safe -- but only worth a second timeout if the channel is
         * actually connected (a reconnect raced the reExecGet); against a
         * down IOC, report after the one timeout like the old code did. */
        dropGetChan(name, request);
        if (!pvaChannelConnected(name)) return PvValue();
        PvaError err2;
        c = openGetChan(name, request, err2);
        if (c) {
            PvValue v2;
            bool retry2 = false;
            PvaError err3;
            if (warmGetExec(name, *c, v2, err3, retry2)) {
                err = PvaError();
                recordChannel(name);
                return v2;
            }
            if (retry2) dropGetChan(name, request);  /* start clean next call */
        }
        return PvValue();                   /* keep the first error */
    } catch (std::exception &e) {
        err.err = PVA_NOTCONNECTED;
        err.msg = std::string("pvaGet '") + name + "': " + e.what();
        pvaSetLastError(err.err, err.msg);
        return PvValue();
    }
}

} // namespace labpva
