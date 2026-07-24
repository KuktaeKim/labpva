# labpva — an EPICS pvAccess interface for MATLAB

labpva is a labca-style MATLAB interface to **pvAccess (PVA)**, the EPICS 7
protocol that is replacing the increasingly obsolete Channel Access (CA) plain
protocol that [labca](https://till-s.github.io/epics-labca) speaks.

It mirrors labca's architecture and verb-for-verb API — `pvaGet`/`pvaPut`/
`pvaSetMonitor`/... in place of `lcaGet`/`lcaPut`/`lcaSetMonitor`/... — so code
ported from labca changes mostly by symbol-swapping the `lca` prefix to `pva`.
The two libraries are designed to coexist on the MATLAB path.

The fundamental difference from labca is **structures**. A CA channel carries
one typed value (a scalar or a waveform). A PVA channel carries a whole
**PVStructure**: an NTScalar wraps its payload in a `.value` field next to
`.alarm`, `.timeStamp`, `.display`, ...; an NTTable is a structure of columns;
an NTNDArray is an image; and a server may publish entirely custom nested
structures. labpva marshals these to and from nested MATLAB structs. See
[ARCHITECTURE.md](ARCHITECTURE.md) for the full design and type-mapping tables.

## Build

Requires EPICS 7 base (with pvAccess/pvData/normativeTypes/pvaClient — bundled
in base ≥ 7) and a MATLAB with the `mex` toolchain. The build uses the standard
EPICS `configure/` layout (like labca): set the two external-product paths in
[`configure/RELEASE`](configure/RELEASE) — `EPICS_BASE` and `MATLABDIR` — then:

```sh
make            # builds the glue lib, the 30 MEX, and the .m help stubs
```

Site-tunable build knobs (C++ standard, the PVA library list, the OS/compiler
header sub-dirs) live in [`configure/CONFIG_SITE`](configure/CONFIG_SITE);
mechanical/derived settings are assembled in
[`configure/CONFIG`](configure/CONFIG). Per-host overrides can go in
`configure/RELEASE.local` / `RELEASE_SITE` without editing the tracked files.

Products land in `bin/<EPICS_HOST_ARCH>/labpva/` (e.g.
`bin/RL8-x86_64/labpva/pvaGet.mexa64`), mirroring labca's `bin/<arch>/labca/`
layout. Alongside the 30 MEX you'll also find **`libmpvaglue.so`** — the shared
glue library that holds the one-per-process channel/monitor registry; every MEX
links against it (see [ARCHITECTURE.md](ARCHITECTURE.md) §6). After rebuilding,
**restart MATLAB** to load the new binaries — labpva calls `mexLock` (so the
EPICS libraries aren't unloaded mid-teardown, which would segfault on exit), and
that means `clear mex` will *not* reload it.

Defaults target the ALS controls host this was developed on: EPICS
`/usr/local/epics/R7.0.10/base`, MATLAB R2025b. Following the EPICS standard,
`EPICS_BASE` lives in `configure/RELEASE` and arch/compiler settings come from
`$(EPICS_BASE)/configure/CONFIG`. The MEX layer itself is still driven directly
by `mex` (not the EPICS O.<arch>/PROD machinery) — see the comment in
`configure/CONFIG`.

## Run (in MATLAB)

```matlab
% Use YOUR built arch. Both RL8-x86_64 and linux-x86_64 are built here
% (the default build arch is linux-x86_64). Do NOT name this variable `labpva`
% -- a workspace variable shadows `help labpva` (MATLAB resolves variables
% before folders).
labpvaRoot = '/path/to/labpva';
addpath(fullfile(labpvaRoot,'bin','linux-x86_64','labpva'));  % MEX + help stubs + libmpvaglue.so
addpath(fullfile(labpvaRoot,'doc'));                          % printpvs / printvals
% No LD_LIBRARY_PATH needed: the MEX carry an RPATH to the EPICS libs. If you
% ever must set it, do so in the shell BEFORE launching MATLAB, pointing at the
% SAME base you built against, e.g.
% /usr/local/epics/R7.0.10/base/lib/linux-x86_64.

v   = pvaGet('labpva:test:ao')            % NTScalar -> the value (drop-in for lcaGet)
[v,ts] = pvaGet('labpva:test:ao')         % ts = sec + i*nsec
wf  = pvaGet('labpva:test:wf')            % NTScalarArray -> row vector
sel = pvaGet('labpva:test:enum')          % NTEnum -> selected choice string
s   = pvaGet('mdach:circle')              % structured/rich PV -> whole nested struct
info = pvaInfo('labpva:test:ao')          % type id + field-tree dump (cf. pvinfo)

pvaPut('labpva:test:ao', 1.25)            % scalar write, waits for completion (cf. lcaPut)
pvaPutNoWait('labpva:test:ao', 1.25)      % fire and forget
clear c; c.value.x = 1.5; c.value.y = -2; % set just these leaves of a structured PV
pvaPutStructure('mdach:circle', c)        % writes value.x/value.y; other fields unchanged

pvaSetMonitor('labpva:test:ao')           % subscribe ONCE, before the poll loop
while ~pvaNewMonitorValue('labpva:test:ao'), pause(0.02); end
latest = pvaGet('labpva:test:ao');        % served from the monitor cache
fresh  = pvaGet('labpva:test:ao', true);  % poll=true -> force a fresh server read
pvaClear()                                % drop all monitors

pvaSetProvider('ca')   % fall back to Channel Access for v3-only names
```

### `pvaGet` vs `pvaGetStructure`

There are two read verbs. Use **`pvaGet`** for almost everything:

- For a **scalar / waveform / enum** it returns the **bare value** — a double, a
  row vector, or the selected choice string — a drop-in for `lcaGet`.
- For a **structured or rich** PV — an NTNDArray image, an NTTable, a custom
  multi-field group, or any PV with no top-level scalar `value` — it returns the
  **whole nested struct**. So for those you rarely need `pvaGetStructure`.

**`pvaGetStructure`** *always* returns the entire PVStructure as a nested struct,
**including for a scalar** (its `value` + `alarm` + `timeStamp` + `display`/…).
Reach for it when you specifically want the full metadata tree of a scalar PV, or
to scope the fetch with a `request` (e.g. `pvaGetStructure(pv,'field(value,alarm)')`).

Both accept a trailing **`poll`** flag: while a monitor is active, reads are
served from the monitor cache; `pvaGet(pv,true)` / `pvaGetStructure(pv,true)`
force a fresh server read. Record DB fields **not** carried over pvAccess
(`NELM`, `NORD`, `FTVL`, …) are read over Channel Access — `lcaGet('PV.NORD')`,
or `pvaSetProvider('ca')` then `pvaGet('PV.NORD')`.

### Printing / monitoring a whole structure

```matlab
s = pvaGet('mdach:circle');    % structured PV -> whole nested struct (smart pvaGet)
printpvs(s,  'mdach:circle')   % EVERY leaf (value, alarm, display, control, ...)
printvals(s, 'mdach:circle')   % only each signal's .value + timeStamp

% monitor + print on each update (subscribe once, poll, clear):
pvaSetMonitor('mdach:circle', 'field()');         % 'field()' monitors the whole structure
for k = 1:50
    if pvaNewMonitorValue('mdach:circle')
        % served from the monitor cache by default (no network round-trip):
        printvals(pvaGet('mdach:circle'), 'mdach:circle')
    end
    pause(0.05);
end
pvaClear('mdach:circle');

% force a fresh server read (bypass the cache) with the trailing poll flag:
s = pvaGet('mdach:circle', true);
```

### Getting help

Once `bin/<arch>/labpva` is on the path, `help labpva` lists every verb
grouped, and `help pvaGet` / `doc pvaGet` (plus tab-completion) work for each —
served by `Contents.m` and the per-verb `.m` stubs alongside the MEX (generated
by `doc/gen_help_stubs.py`). Caveat: if `help labpva` prints
"labpva is a variable…", you named your path variable `labpva`/`LABPVA`;
`clear` it (a workspace variable shadows the folder).

After **rebuilding** the MEX, **restart MATLAB** so the new binaries load.
labpva `mexLock`s itself (so the EPICS libraries stay mapped and MATLAB doesn't
segfault on exit), which means `clear mex` will *not* unload or reload it.

### Auto-load at every launch

Put the path setup in MATLAB's `startup.m` (in your `userpath`, e.g.
`~/Documents/MATLAB/startup.m`) so labpva loads automatically — no manual
`addpath` per session:

```matlab
labpvaRoot = '/path/to/labpva';                             % NOT named `labpva`
addpath(fullfile(labpvaRoot,'bin','RL8-x86_64','labpva'));  % use your built arch
addpath(fullfile(labpvaRoot,'doc'));
clear labpvaRoot
```

No `LD_LIBRARY_PATH` line is needed (the MEX `RPATH` finds the EPICS libs); if it
ever is, it must be set in the shell *before* launching MATLAB, not in
`startup.m`.

## Functions

Every verb accepts a single PV name **or a cell array of names** (returning an
`N×1` column) unless noted. Timestamps are complex doubles (real = seconds past
the UNIX epoch, imag = nanoseconds). `type` is a labca-style letter
(`N B S L F D C`; `N` = native, `C` = string). `poll=true` forces a fresh server
read past the monitor cache. `help <verb>` gives per-verb detail.

**Read / write**

| function | description |
| --- | --- |
| `pvaGet(pv [,type] [,poll])` | Read a channel: the `.value` for a scalar/array/enum, or the whole nested struct for a structured/rich PV. `[v,ts]=…` also returns the timestamp. |
| `pvaGetStructure(pv [,request] [,poll])` | Always return the **entire** PVStructure as a nested struct (even a scalar's). `request` = pvRequest (default `field()`). |
| `pvaPut(pv, value [,type])` | Write the `.value` field, **wait** for completion (drop-in for `lcaPut`). Enums accept a choice string or an index. |
| `pvaPutNoWait(pv, value [,type])` | Write without waiting for completion. |
| `pvaPutStructure(pv, s [,request])` | Write a whole structure from a MATLAB struct; only the fields present in `s` are written (others keep their value). |
| `pvaInfo(pv)` | Introspect: struct with `.name`, `.typeid`, and a field-tree `.introspection` dump (cf. `pvinfo`). |

**Monitors**

| function | description |
| --- | --- |
| `pvaSetMonitor(pv [,request])` | Subscribe to value changes (call once, before the poll loop). |
| `pvaNewMonitorValue(pv)` | Non-blocking: `true` once per newly arrived sample; drains the queue into the cache. |
| `pvaNewMonitorWait(pv [,timeout])` | Block up to `timeout` s (0 = configured default) for the next sample; `false` on timeout. |
| `pvaClear([pv])` | Tear down one monitor, or all if no name. The channel connection stays cached. |

**Introspection (read-only)**

| function | description |
| --- | --- |
| `pvaMonitors()` | Cell of PV names that currently have an active monitor. |
| `pvaIsMonitored(pv)` | Logical: is a monitor active on the name? (non-destructive) |
| `pvaChannels()` | Cell of PV names labpva has opened a channel for this session (a superset of `pvaMonitors`). |
| `pvaIsConnected(pv)` | Logical: is that channel currently connected to its IOC? |

**Metadata** (from NT sub-fields; return `NaN`/`''` when the field is absent)

| function | description |
| --- | --- |
| `pvaGetStatus(pv)` | `[severity, status, ts]` from the NT `alarm` field. |
| `pvaGetNelem(pv)` | Element count of `value` (1 for scalar/enum, array length otherwise). |
| `pvaGetControlLimits(pv)` | `[lo, hi]` drive-range limits (`control.*`). |
| `pvaGetGraphicLimits(pv)` | `[lo, hi]` display-range limits (`display.*`). |
| `pvaGetAlarmLimits(pv)` | `[lo, hi]` alarm thresholds (`valueAlarm.*`). |
| `pvaGetWarnLimits(pv)` | `[lo, hi]` warning thresholds (`valueAlarm.*`). |
| `pvaGetUnits(pv)` | Engineering-units string (`display.units`). |
| `pvaGetPrecision(pv)` | Display precision (`display.precision`). |
| `pvaGetEnumStrings(pv)` | Cell of an NTEnum's choice strings. |

**Configuration / diagnostics**

| function | description |
| --- | --- |
| `pvaSetTimeout(sec)` / `pvaGetTimeout()` | Get/set the connect/IO timeout, in seconds. |
| `pvaSetProvider(p)` / `pvaGetProvider()` | `'pva'` (default) or `'ca'` for subsequently-opened channels. |
| `pvaDebugOn()` / `pvaDebugOff()` | Toggle pvaClient + labpva debug output. |
| `pvaLastError()` | `[code, message]` of the last operation (`code` 0 = ok). |

**MATLAB helpers** (in `doc/`, plain `.m` functions, not MEX)

| function | description |
| --- | --- |
| `pvaGetImage(pv [,poll])` | NTNDArray → a display-ready 2-D (mono) / 3-D (color) image array; `[img,info]` also gives offset-aware axis vectors. Uncompressed + JPEG. |
| `printpvs(s, name)` | Print **every** leaf of a fetched structure (value + all metadata). |
| `printvals(s, name)` | Print only each signal's `.value` + a readable timestamp. |

## Smoke test

`testing/` has a tiny IOC database exercising the main marshalling paths
(scalar, array, enum, string, bool). With EPICS base on your `PATH`:

```sh
cd testing && softIocPVA -d labpvaTest.db
```

then drive it from MATLAB with the calls above.

## Status

The 30 MEX and the glue layer build cleanly against **EPICS 7.0.10** and **MATLAB
R2025b** (a sibling copy targets R2026a), for both `RL8-x86_64` and
`linux-x86_64`. labpva is **verified working live** against ALS IOCs: reads
(`pvaGet`/`pvaGetStructure`), monitors (`pvaSetMonitor` → `pvaNewMonitorValue` →
cache-served read), the metadata getters, NTEnum reads, custom Q:group
structures and areaDetector NTNDArray images, and the `help` system.

Since the first bring-up (2026-06-11, against a `mdach:circle` softIocPVA), the
build moved to the standard EPICS `configure/` layout; `pvaGet`/`pvaGetStructure`
gained monitor-cache-by-default plus a `poll` flag; `pvaGet` became "smart" (bare
value for scalar/array/enum, whole struct for rich PVs); and a `mexLock` fix
removed a segfault on MATLAB exit. See [CHANGELOG.md](CHANGELOG.md). **Not yet
exercised live:** the write path (`pvaPut`/`pvaPutNoWait`/`pvaPutStructure`).
Known limitations/follow-ups are at the end of [ARCHITECTURE.md](ARCHITECTURE.md).
