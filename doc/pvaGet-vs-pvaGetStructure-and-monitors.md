# pvaGet vs pvaGetStructure, caching, and monitor semantics

A design/behaviour note distilled from a working session (2026-06-23). Covers
what the two read verbs return, how the network/caching layers actually behave,
pvAccess monitor delta semantics, and a proposed opt-in guarded cache for
`pvaGetStructure`. Companion to `../ARCHITECTURE.md` (§4 monitors, §6 caching).

## 1. pvaGet vs pvaGetStructure — what you get

One-line rule:

- **`pvaGet('X')`** → unwraps and returns the **`.value` field only**, with
  NT-aware sugar.
- **`pvaGetStructure('X')`** → returns the **entire tree** (`value` + `alarm` +
  `timeStamp` + `display` + `control` + …) as a nested MATLAB struct.

So roughly `pvaGet('X')` ≈ `pvaGetStructure('X').value`, modulo the NT unwrapping
in the table below.

### Example: `mdach:circle` (its `value` is itself a sub-structure `{angle,x,y}`)

```matlab
>> v = pvaGet('mdach:circle')          % value is a structure -> pvFieldToMx(value)
v =
  struct with fields:
    angle: 0.7854
        x: 1.7320
        y: -0.5000                      % just the value sub-tree; no alarm/timeStamp/metadata

>> s = pvaGetStructure('mdach:circle')  % field() -> whole tree
s =
  struct with fields:
        value: [1x1 struct]   % .angle .x .y
        alarm: [1x1 struct]   % .severity .status .message
    timeStamp: [1x1 struct]   % .secondsPastEpoch .nanoseconds .userTag
      display: [1x1 struct]   % .limitLow .limitHigh .units .precision ...
>> s.value.x
ans = 1.7320
```

`s.value` is exactly what `pvaGet` returned; `pvaGetStructure` adds everything
sitting next to `value`.

### Example: plain NTScalar `mdach:ao` (value is a number) — the labca-faithful case

```matlab
>> v = pvaGet('mdach:ao')              % -> bare double, like lcaGet
v = 1.2500
>> s = pvaGetStructure('mdach:ao')     % -> struct with .value=1.25, .alarm, .timeStamp, .display, .control
```

### NT sugar (why pvaGet is not *literally* s.value)

| X is an…       | `pvaGet('X')` returns        | `pvaGetStructure('X').value` is        |
| -------------- | ---------------------------- | -------------------------------------- |
| NTScalar       | the number (double)          | the same number                        |
| NTScalarArray  | row vector                   | the same vector                        |
| **NTEnum**     | the **selected choice string** (e.g. `'Open'`) | `struct(index, choices{}, choice)` |
| struct value (circle) | the `{angle,x,y}` struct | the same struct                       |
| no `value` field | the **whole tree** (falls through) | the whole tree                   |

Code path: `pvValueToMx` (`glue/pvaConvert.cpp:228`) finds `value`
(`:232`); if absent it returns the whole tree (`:233`); enum_t gets the
choice-string sugar (`:240`); otherwise it marshals `value` recursively
(`:258/276`). `pvaGetStructure` uses `pvStructureToMx` (`:222`) on the whole tree.

### What crosses the wire

- `pvaGet` requests `field(value,alarm,timeStamp)` (`matlab/pvaGet.cpp:36`) — it
  fetches alarm+timeStamp too but only *returns* value; timeStamp feeds the
  optional `[v,ts]` 2nd output, alarm is discarded. A small fixed slice.
- `pvaGetStructure` requests `field()` — the **entire** structure including the
  heavy `display`/`control`/`valueAlarm` metadata.

That byte-size gap is why caching `pvaGetStructure` saves more per call than
caching `pvaGet`.

## 2. The three caching/network layers (no UDP search per call)

Both verbs funnel through the same glue core `labpva::pvaGet(name, request, err,
useMonitorCache)` (`glue/pvaGlue.cpp:62`). Differences: `pvaGet` passes
`request="field(value,alarm,timeStamp)"`, `useMonitorCache=true`;
`pvaGetStructure` passes `request="field()"`, `useMonitorCache=false` (default,
`pvaGlue.h:51`).

1. **Channel connection cache (the UDP-search layer).** `client()->channel(name,
   provider, timeout)` consults pvaClient's channel cache keyed by
   `name+provider`. **First touch** of a name does the UDP channel *search*, opens
   a **TCP** connection, caches the channel. **Every later call** — `pvaGet` *or*
   `pvaGetStructure` — reuses that open TCP channel. **UDP search is once per name
   per session, not per call.** Works session-wide because `g_client` is the single
   shared `PvaClient` (the `libmpvaglue.so` fix).
2. **Get-request handle cache.** `PvaClientChannel` caches its `PvaClientGet`
   handle keyed by the pvRequest string, so `pvaGet`'s `field(value,...)` handle
   and `pvaGetStructure`'s `field()` handle are separate cached handles on the
   same channel.
3. **Monitor value cache (`g_monitors`).** Only exists if you called
   `pvaSetMonitor`. Holds a deep-copied `latest` sample per monitored name
   (`pvaGlue.cpp:42-50`), refreshed only when you poll via `pvaNewMonitorValue`/
   `pvaNewMonitorWait`. **`pvaGet` opts in** (`useMonitorCache=true`): an active
   monitor → returns the cached sample with zero network traffic.
   **`pvaGetStructure` does NOT** (`false`): always a fresh `field()` read.

### Why pvaGetStructure reads fresh by default

The monitor cache holds only the fields the monitor's *request* asked for. A
monitor with the default/narrow request omits `display`/`control`/`valueAlarm`,
so serving `pvaGetStructure` from it would return a **partial tree** → silent
NaN/""/0 metadata. That was a real bug; the code-review fix made structure and
metadata reads always-fresh (`useMonitorCache=false`).

## 3. pvAccess monitor delta semantics (e.g. `field(value,alarm,timeStamp)`)

**What triggers an update:** the request defines the watched subset. The IOC
fires only when a field *inside* the set changes. A change to `value`/`alarm`/
`timeStamp` fires; a change to `display`/`control`/`valueAlarm` does **not**
(you didn't subscribe to them) — another reason narrow monitoring is lighter.

**What crosses the wire — a delta, not all three each time.** pvAccess sends a
**changed-bitset** + data for **only the changed fields**, not the whole subset.
The **first** event after connecting is the full initial snapshot (all bits set).
In practice: `value` and `timeStamp` travel together (record processing
restamps on every value update); `alarm` is sent only when severity/status
actually changes.

**What labpva hands you.** pvaClient maintains the complete current subset state
internally and applies each delta to it; `e.mon->getData()->getPVStructure()`
returns the **full current** `{value,alarm,timeStamp}` with current values for
all three, which labpva deep-copies into `latest` (`pvaGlue.cpp:161`). So you
never see partial data at the MATLAB layer regardless of which field triggered.
(labpva does not currently expose the changed-bitset — only current values.)

**Coalescing.** The poll drains the queue keeping only the **last** sample
(`pvaGlue.cpp:160`, the `while (e.mon->poll())` loop). N changes between two
`pvaNewMonitorValue` calls collapse to the latest — standard "latest value"
monitor semantics (like labca/ezca), not a lossless event stream.

## 4. Proposed: opt-in guarded cache for pvaGetStructure (NOT yet implemented)

Keep the always-fresh default (no change for existing callers); add an opt-in
flag that turns on a *guarded* cache only when asked.

### API

```matlab
s = pvaGetStructure(pv [,request] [,useCache])
```

- `pvaGetStructure('X')` → fresh `field()` read (today's behaviour; default)
- `pvaGetStructure('X', true)` → opt into the guarded cache, default request
- `pvaGetStructure('X','field()',true)` → explicit request + cache

Arg parsing mirrors labca style: a `char` arg is `request`, a trailing `logical`
is `useCache` (default false).

### Guard — the cache is served only when complete (else transparent fresh read)

Serve from cache iff `useCache` is true AND an active monitor exists AND it has a
polled sample AND **either**:

1. the monitor request is whole (`field()` / empty) — subsumes any get request, **or**
2. the get request **equals** the monitor request (normalised string compare).

So `pvaSetMonitor('X','field(value,alarm,timeStamp)')` +
`pvaGetStructure('X','field(value,alarm,timeStamp)',true)` → cache-served
(matches). But `... + pvaGetStructure('X', true)` (default `field()`) → falls
back to a fresh full read (cache lacks display/control). Always correct; worst
case an unnecessary fresh read. (Full field-by-field subset detection would need
a small pvRequest parser — deferred.)

### Recommended narrow-monitor pattern (light updates + cache-served reads)

```matlab
req = 'field(value,alarm,timeStamp)';      % or 'field(value,timeStamp)' for printvals
pvaSetMonitor('mdach:circle', req);
for k = 1:50
    if pvaNewMonitorValue('mdach:circle')
        s = pvaGetStructure('mdach:circle', req, true);   % cache-served, matches monitor
        printvals(s, 'mdach:circle')
    end
    pause(0.05);
end
pvaClear('mdach:circle');
```

### Implementation footprint (small, low-risk)

- `MonEntry` gains `std::string request` (set in `pvaMonitorSet`; currently discarded).
- Glue `pvaGet` gains `requireWholeMonitor=false`; its cache branch
  (`pvaGlue.cpp:71-75`) additionally checks the stored monitor request.
  **`pvaGet`'s own behaviour is untouched** (passes false).
- `pvaGetStructure.cpp` parses the trailing `logical`; when set, calls with
  `useMonitorCache=true, requireWholeMonitor=true`.
- Regenerate the `pvaGetStructure` help stub for the new signature.
- Build, `clear mex`, live retest against `mdach:circle`.

## 5. Known doc discrepancy to fix alongside this

The skill `epics-matlab-labca-labpva/SKILL.md` is **stale**: it claims
`pvaGetStructure` is already cache-served while a monitor is active (≈ lines 127
and 151, "both pvaGet and pvaGetStructure … return the cached value"). After the
code review that is **false** — `pvaGetStructure` is always-fresh
(`pvaGlue.h:51`; `ARCHITECTURE.md §4` is correct). Fix the skill to match, or
update it when the opt-in flag lands.
