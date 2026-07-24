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

v   = pvaGet('labpva:test:ao')            % NTScalar value (drop-in for lcaGet)
[v,ts] = pvaGet('labpva:test:ao')         % ts = sec + i*nsec
wf  = pvaGet('labpva:test:wf')            % NTScalarArray -> row vector
sel = pvaGet('labpva:test:enum')          % NTEnum -> selected choice string

s   = pvaGetStructure('labpva:test:ao')   % the WHOLE structure as a nested struct
info = pvaInfo('labpva:test:ao')          % type id + field-tree dump (cf. pvinfo)

pvaPut('labpva:test:ao', 1.25)            % waits for completion (cf. lcaPut)
pvaPutNoWait('labpva:test:ao', 1.25)      % fire and forget
pvaPutStructure('labpva:test:ao', s)      % write a whole structure back

pvaSetMonitor('labpva:test:ao')           % subscribe ONCE, before the poll loop
while ~pvaNewMonitorValue('labpva:test:ao'), pause(0.02); end
latest = pvaGet('labpva:test:ao');        % served from the monitor cache
fresh  = pvaGet('labpva:test:ao', true);  % poll=true -> force a fresh server read
pvaClear()                                % drop all monitors

pvaSetProvider('ca')   % fall back to Channel Access for v3-only names
```

`pvaGet` returns a bare value for scalars/arrays/enums and the **whole nested
struct** for richer PVs (NTNDArray images, NTTable, custom groups) — so for those
you don't need `pvaGetStructure`. While a monitor is active, reads are served
from its cache; pass a trailing `true` (`poll`) to force a fresh server read
(e.g. `pvaGet(pv, true)`). Record DB fields not carried over pvAccess (`NELM`,
`NORD`, `FTVL`, …) are read over Channel Access — `lcaGet('PV.NORD')`, or
`pvaSetProvider('ca')` then `pvaGet('PV.NORD')`.

### Printing / monitoring a whole structure

```matlab
s = pvaGetStructure('mdach:circle');
printpvs(s,  'mdach:circle')   % EVERY leaf (value, alarm, display, control, ...)
printvals(s, 'mdach:circle')   % only each signal's .value + timeStamp

% monitor + print on each update (subscribe once, poll, clear):
pvaSetMonitor('mdach:circle', 'field()');         % 'field()' monitors the whole structure
for k = 1:50
    if pvaNewMonitorValue('mdach:circle')
        % served from the monitor cache by default (no network round-trip),
        % because the monitor's 'field()' request covers the whole structure:
        printvals(pvaGetStructure('mdach:circle'), 'mdach:circle')
    end
    pause(0.05);
end
pvaClear('mdach:circle');

% pvaGetStructure is cache-served by default (like pvaGet) whenever a monitor is
% active and its request covers the requested fields; otherwise it reads fresh.
% Force a fresh server read with the trailing poll flag:
s = pvaGetStructure('mdach:circle', true);                 % poll: bypass the cache
s = pvaGetStructure('mdach:circle', 'field()', true);      % explicit request + poll
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
