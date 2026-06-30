# labpva architecture & labca→PVA mapping

This document explains how labpva mirrors labca, how it maps the pvAccess
data model onto MATLAB types, and the exact API contract. It is meant to be
readable without the source.

## 1. Why a new library

[labca](https://till-s.github.io/epics-labca) is a MATLAB MEX interface to
**Channel Access (CA)**, the original EPICS network protocol. CA carries one
typed value per channel (a scalar or a waveform) plus a fixed set of metadata
(alarm, timestamp, control/display limits, units, precision, enum strings).
labca's stack is:

```
MATLAB  ─▶  lcaGet.mexa64  ─▶  libmezcaglue  ─▶  libezca  ─▶  libca / libCom
            (one MEX per verb)  (multi-PV glue)  (EZCA)     (Channel Access)
```

EPICS 7 introduces **pvAccess (PVA)**, which is replacing CA. PVA channels
carry an arbitrary, self-describing **PVStructure** rather than a bare value.
The standard shapes are the *normative types* (NTScalar, NTScalarArray,
NTEnum, NTTable, NTNDArray, ...), but a server may publish any structure.

labpva keeps labca's shape but swaps the bottom two layers for the PVA stack:

```
MATLAB  ─▶  pvaGet.mexa64  ─▶  libmpvaglue   ─▶  pvaClientCPP ─▶ libpvAccess / libpvData / libnt
            (one MEX per verb)  (glue + struct  (sync wrapper   (pvAccess + pvData + normativeTypes)
                                 marshalling)    over PVA)
```

Concretely the glue layer is four objects in `glue/`:

| labpva file       | role                                                        | labca analogue            |
| ----------------- | ----------------------------------------------------------- | ------------------------- |
| `pvaError.*`      | error codes + process-global last error                     | `lcaError.*`              |
| `pvaConvert.*`    | **PVStructure ↔ mxArray marshalling (the new core)**        | (no analogue)             |
| `pvaGlue.*`       | PvaClient singleton, channel cache, monitor registry, get/put | `multiEzca.*` + ezca cache |
| `mglue.*`         | MEX argument parsing, error funnel, output assembly         | `mglue.*`                 |

## 2. The structure model (the central difference)

`pvaGet` keeps labca ergonomics: for an NTScalar it returns the `.value`
field, so `pvaGet('X')` behaves like `lcaGet('X')`. But every channel is really
a structure, and `pvaGetStructure` returns the **whole tree** as a nested
MATLAB struct.

### PV → MATLAB

| pvData field                  | MATLAB result                                             |
| ----------------------------- | --------------------------------------------------------- |
| scalar, numeric               | `1×1 double` (all int widths collapse to double)          |
| scalar, boolean               | `1×1 logical`                                             |
| scalar, string                | char row vector                                           |
| scalarArray, numeric          | `1×N double` row vector                                   |
| scalarArray, string           | `1×N cell` of char                                        |
| scalarArray, boolean          | `1×N logical`                                             |
| structure                     | `1×1 struct`, one field per sub-field (recursive)         |
| structureArray                | `1×N struct array` (homogeneous fields)                   |
| union                         | the stored field, unwrapped (`[]` if empty)               |
| unionArray                    | `1×N cell`                                                 |
| `enum_t` sub-structure        | `struct(index, choices{}, choice)` — NT-aware sugar       |

Field names that are not legal MATLAB identifiers are sanitised (non-`[A-Za-z0-9_]`
→ `_`, leading non-letter prefixed with `f_`, truncated to 63 chars) and
de-duplicated; on write-back the target structure's own field list is
authoritative, so round-tripping is name-safe.

### MATLAB → PV (`pvaPut`, `pvaPutStructure`)

The **target** structure's introspection is authoritative — labpva coerces the
MATLAB value to each field's native scalar type via pvData's generic
`putFrom<T>`. A MATLAB struct is matched to a PV structure by (sanitised) field
name; fields you omit keep their fetched value. `pvaPut` writes only `value`;
`pvaPutStructure` writes the whole tree (and marks the whole structure changed).

### Normative-type handling

| NT type        | `pvaGet` returns                          | full structure via `pvaGetStructure` |
| -------------- | ----------------------------------------- | ------------------------------------- |
| NTScalar       | the scalar value                          | value + alarm/timeStamp/display/...   |
| NTScalarArray  | the waveform (row vector)                 | as above                              |
| NTEnum         | selected choice **string** (or index if a numeric type is requested) | `value.{index,choices,choice}` |
| NTTable        | **the whole structure** (labels + columns)| labels + per-column arrays            |
| NTNDArray      | **the whole structure** (image)           | value(union)/dimension[]/codec/...    |
| custom         | `.value` if it is a scalar/array/enum, else **the whole structure** | the whole tree |

`pvaGet` returns a **bare value only for a scalar, scalar-array, or enum**
`value` field (the labca-faithful cases — the thing you compute with). For
anything richer — a `union` value (NTNDArray image), a structured value
(NTTable), a structure array, or a PV with no top-level `value` — it returns the
**whole nested structure**, identical to `pvaGetStructure`. So `pvaGet` covers
both cases; `pvaGetStructure` remains only for forcing the full tree of a
scalar PV (e.g. to read `value`+`alarm`+`display` together). To make this work,
the `pvaGet` value verb fetches `field()` (the whole structure), not just
`field(value,alarm,timeStamp)`.

Metadata getters (`pvaGetControlLimits`, `pvaGetUnits`, ...) read the standard
NT property sub-fields (`control.*`, `display.*`, `valueAlarm.*`). In CA these
came from `DBR_CTRL_*` requests; in PVA they are just ordinary sub-fields, so
labpva fetches the structure once and reads the paths.

## 3. Timestamps

Like labca, a timestamp is returned as a **complex double**: real part =
seconds past the UNIX epoch, imaginary part = nanoseconds. The packing
(`complexColumn` in `mglue.cpp`) is written against both the classic
(`mxGetPi`) and the interleaved (`mxGetComplexDoubles`) MATLAB complex APIs, so
it builds whether `mex` defaults to `-R2017b` or `-R2018a`.

```matlab
[v, ts] = pvaGet('X');
when    = datetime(real(ts), 'ConvertFrom', 'posixtime') + seconds(imag(ts)/1e9);
```

## 4. Monitors

`pvaSetMonitor` subscribes; the glue keeps a registry keyed by channel name.
`pvaNewMonitorValue` drains pending events (keeping the latest), caches a deep
copy, and returns true once per arrived sample — like `lcaNewMonitorValue`.
`pvaNewMonitorWait` blocks. A monitored channel is then served from its cache
by **both `pvaGet` and `pvaGetStructure`** (each passes `useMonitorCache=true`),
so a read right after a positive `pvaNewMonitorValue` returns that sample with
no extra round-trip — matching ezca's behaviour. `pvaClear` tears the monitor
down (the pvaClient channel stays cached, so the connection persists).

`pvaGetStructure` adds a **coverage guard** (`requireWholeMonitor=true`): the
cache is served only if the monitor's pvRequest actually covers the requested
fields — its request is whole (`field()` / empty), or identical to the structure
request. Otherwise it falls through to a fresh read, so an active monitor whose
request omits `display`/`control`/`valueAlarm` can never silently feed a partial
structure (NaN/""/0 metadata). Callers can force a fresh read with the trailing
`poll` flag: `pvaGetStructure(pv [,request], true)`. The **metadata getters**
(`pvaGetUnits`, `pvaGetControlLimits`, …) still pass `useMonitorCache=false` and
always read fresh.

The `pvaGet` value verb leaves the guard off (`requireWholeMonitor=false`): its
only need, `value`, is present in any reasonable monitor, so it serves the cache
unconditionally when one is active. Like `pvaGetStructure`, it accepts a trailing
`poll` flag — `pvaGet(pv [,type], true)` — to force a fresh server read without
dropping the monitor (equivalent to reading after `pvaClear`).

This registry is process-global state shared across separate MEX files
(`pvaSetMonitor` registers, `pvaNewMonitorValue` polls), so it **must** live in
a single shared library — see §6. The monitor remembers the pvRequest it
subscribed with (`MonEntry.request`), which is what the coverage guard checks.
Subscribing with `'field()'` caches the whole structure; a narrower request
(e.g. `'field(value,alarm,timeStamp)'`) caches only those fields, and a
cache-served `pvaGetStructure` is then allowed only for a matching request.

## 5. Full API reference

`type` is a labca-style letter: `N` native (default), `B/S/L/F/D` numeric
widths, `C` string. (PVA converts generically, so the width letters mostly just
distinguish "numeric" from "string"; `C` forces string presentation.)

| labpva                         | labca analogue          | signature                                              |
| ------------------------------ | ----------------------- | ------------------------------------------------------ |
| `pvaGet`                       | `lcaGet`                | `[val,ts] = pvaGet(pv[s] [,type])`                     |
| `pvaGetStructure`              | — (new)                 | `s = pvaGetStructure(pv[s] [,request])`                |
| `pvaPut`                       | `lcaPut`                | `pvaPut(pv[s], value [,type])`                         |
| `pvaPutNoWait`                 | `lcaPutNoWait`          | `pvaPutNoWait(pv[s], value [,type])`                   |
| `pvaPutStructure`              | — (new)                 | `pvaPutStructure(pv[s], struct [,request])`            |
| `pvaInfo`                      | — (new; cf. `pvinfo`)   | `s = pvaInfo(pv)` → `.name/.typeid/.introspection`     |
| `pvaSetMonitor`                | `lcaSetMonitor`         | `pvaSetMonitor(pv[s] [,request])`                      |
| `pvaNewMonitorValue`           | `lcaNewMonitorValue`    | `tf = pvaNewMonitorValue(pv[s])`                       |
| `pvaNewMonitorWait`            | `lcaNewMonitorWait`     | `[tf] = pvaNewMonitorWait(pv[s] [,timeout])`           |
| `pvaClear`                     | `lcaClear`              | `pvaClear([pv[s]])`                                    |
| `pvaGetStatus`                 | `lcaGetStatus`          | `[sev,sta,ts] = pvaGetStatus(pv[s])`                   |
| `pvaGetNelem`                  | `lcaGetNelem`           | `n = pvaGetNelem(pv[s])`                               |
| `pvaGetControlLimits`          | `lcaGetControlLimits`   | `[lo,hi] = pvaGetControlLimits(pv[s])`                 |
| `pvaGetGraphicLimits`          | `lcaGetGraphicLimits`   | `[lo,hi] = pvaGetGraphicLimits(pv[s])`                 |
| `pvaGetAlarmLimits`            | `lcaGetAlarmLimits`     | `[lo,hi] = pvaGetAlarmLimits(pv[s])`                   |
| `pvaGetWarnLimits`             | `lcaGetWarnLimits`      | `[lo,hi] = pvaGetWarnLimits(pv[s])`                    |
| `pvaGetUnits`                  | `lcaGetUnits`           | `u = pvaGetUnits(pv[s])`                               |
| `pvaGetPrecision`              | `lcaGetPrecision`       | `p = pvaGetPrecision(pv[s])`                           |
| `pvaGetEnumStrings`            | `lcaGetEnumStrings`     | `c = pvaGetEnumStrings(pv[s])`                         |
| `pvaSetTimeout`/`pvaGetTimeout`| `lcaSet/GetTimeout`     | seconds                                                |
| `pvaSetProvider`/`pvaGetProvider`| — (new)               | `'pva'` or `'ca'` per-channel provider                 |
| `pvaDebugOn`/`pvaDebugOff`     | `lcaDebugOn/Off`        | toggle pvaClient + labpva debug                        |
| `pvaLastError`                 | `lcaLastError`          | `[code,msg] = pvaLastError()`                          |

Where labca takes an `nelem` argument, labpva omits it: PVA returns the whole
array. Where a single PV name is passed, output is the value itself; for a cell
of names, output is an `N×1` numeric column when every value is a real scalar,
otherwise an `N×1` cell.

labca verbs intentionally **not** carried over: `lcaSetRetryCount`/
`lcaGetRetryCount` (CA-specific retry model; PVA reconnects internally),
`lcaSetSeverityWarnLevel` (CA console-warning knob), `lcaDelay` (use MATLAB
`pause`). New verbs with no labca analogue: `pvaGetStructure`, `pvaPutStructure`,
`pvaInfo`, `pvaSetProvider`/`pvaGetProvider`.

## 6. Process-global state, the shared glue library, and connection caching

labpva keeps state that must outlive a single MEX call and be shared across
*different* verbs:

- the **channel/monitor registry** + the single `PvaClient` (`g_monitors`,
  `g_client` in `pvaGlue.cpp`),
- the **provider** and **timeout** settings (`g_provider`, `g_timeout`),
- the **last-error** code/message (`pvaError.cpp`).

Each `pva*` verb is its own `.mexa64`, which MATLAB `dlopen`s as a separate
module with private symbols. So this state **must** be defined exactly once, in
a shared library all the MEX link against — otherwise each MEX gets its own
copy and, e.g., a monitor set by `pvaSetMonitor` is invisible to
`pvaNewMonitorValue` (this was a real bug; see CHANGELOG 2026-06-11). The build
therefore links the two stateful, MATLAB-symbol-free objects (`pvaGlue.o` +
`pvaError.o`) into **`bin/<arch>/labpva/libmpvaglue.so`**; the MEX add
`-lmpvaglue` (with an rpath to that dir). The `mx*`-using helpers
(`pvaConvert.o`, `mglue.o`) are stateless and stay static per-MEX. This mirrors
labca's `libezca.so` exactly.

**Connection caching.** Every verb opens channels through
`PvaClient::channel(name, provider, timeout)`, which consults a channel cache
keyed by `name+provider`: the first access connects, every later get/put/monitor
on that name **reuses the open connection** (the ezca model). Because `g_client`
is now a single shared instance, the first touch by *any* verb connects and all
others ride that connection for the life of the session.

The caching goes one level deeper: `PvaClientChannel` also caches its
`PvaClientPut`/`PvaClientGet` request objects, keyed by the pvRequest string
(`pvaClientChannel.cpp`). So the *first* `pvaPut('X',…)` creates and connects the
put handle and does one initial value get; **every later** `pvaPut('X',…)` is a
cache hit — labpva just writes the new value into the cached handle and issues
the put. A warm `pvaPut` therefore costs a single round-trip (the write itself;
`pvaPutNoWait` doesn't even wait for the ack), and a warm `pvaGet` a single read
round-trip. No per-call channel/handle re-creation.

## 6b. Build & API-mode notes

- The build uses the standard EPICS `configure/` layout (like labca): external
  product paths in `configure/RELEASE` (`EPICS_BASE`, `MATLABDIR`), site knobs in
  `configure/CONFIG_SITE`, and `configure/CONFIG` includes
  `$(EPICS_BASE)/configure/CONFIG` so arch/compiler vars come from base. The MEX
  layer itself is still built directly by `mex` (the compile/link rules in
  `glue/Makefile` and `matlab/Makefile`), **not** the EPICS O.<arch>/PROD
  harness — simpler to read and retarget, and the rules just read the variables
  assembled in `configure/CONFIG`. The glue is compiled once with `mex -c` so it
  shares MATLAB's exact ABI; `libmpvaglue.so` is then linked from those objects
  with `g++ -shared` (the two stateful objects reference no MATLAB symbols, so a
  plain shared link suffices).
- EPICS 7 is C++11; `std::tr1::shared_ptr` is aliased to `std::shared_ptr` by
  EPICS's `sharedPtr.h`, so the casts compile under modern GCC.
- Complex (timestamp) creation is written for both MATLAB complex APIs, so the
  default `mex` mode in R2018a+ is fine.

## 7. Known limitations / follow-ups

1. **Live-IOC validation (2026-06-11): read/monitor paths verified; write path
   not yet.** Verified working against a live softIocPVA (`mdach:circle`): reads,
   `pvaGetStructure`, the monitor path (`pvaSetMonitor` → `pvaNewMonitorValue` →
   cache-served read), the metadata getters, and NTEnum reads. Still to exercise
   end-to-end: the write path (`pvaPut`/`pvaPutNoWait`/`pvaPutStructure`) and
   array/string round-trips. (This bring-up fixed the per-MEX-state bug — §6 —
   and a code review fixed monitor-cache leakage into metadata reads — §4.)
2. **NTNDArray** comes back as the raw structure (value-union + dimension
   array). A dedicated `pvaGetImage` that reshapes pixels by `dimension[]` and
   decodes the codec would be a natural addition.
3. **RPC** (`channelRPC`) is not exposed yet; pvaClient supports it
   (`PvaClientRPC`) and a `pvaRPC(pv, requestStruct, argStruct)` verb would slot
   in cleanly.
4. **Error-on-longjmp**: like labca, a failed call ends in `mexErrMsgIdAndTxt`,
   which longjmps; C++ stack objects are released before that point in each MEX
   entry, but a hard failure can still leak small std container temporaries.
5. **Provider switching** affects channels opened *after* the call; channels
   already in pvaClient's cache keep their provider.
