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
make            # builds glue objects, then one MEX per pva*.cpp
```

Site-tunable build knobs (C++ standard, the PVA library list, the OS/compiler
header sub-dirs) live in [`configure/CONFIG_SITE`](configure/CONFIG_SITE);
mechanical/derived settings are assembled in
[`configure/CONFIG`](configure/CONFIG). Per-host overrides can go in
`configure/RELEASE.local` / `RELEASE_SITE` without editing the tracked files.

Products land in `bin/<EPICS_HOST_ARCH>/labpva/` (e.g.
`bin/RL8-x86_64/labpva/pvaGet.mexa64`), mirroring labca's `bin/<arch>/labca/`
layout. Alongside the 26 MEX you'll also find **`libmpvaglue.so`** — the shared
glue library that holds the one-per-process channel/monitor registry; every MEX
links against it (see [ARCHITECTURE.md](ARCHITECTURE.md) §6). After rebuilding,
`clear mex` (or restart) in any open MATLAB session to load the new binaries.

Defaults target the ALS controls host this was developed on: EPICS
`/usr/local/epics/R7.0.10/base`, MATLAB R2025b. Following the EPICS standard,
`EPICS_BASE` lives in `configure/RELEASE` and arch/compiler settings come from
`$(EPICS_BASE)/configure/CONFIG`. The MEX layer itself is still driven directly
by `mex` (not the EPICS O.<arch>/PROD machinery) — see the comment in
`configure/CONFIG`.

## Run (in MATLAB)

```matlab
% Use YOUR built arch (RL8-x86_64 on the ALS controls host; linux-x86_64
% elsewhere). Do NOT name this variable `labpva` -- a workspace variable
% shadows `help labpva` (MATLAB resolves variables before folders).
labpvaRoot = '/path/to/labpva';
addpath(fullfile(labpvaRoot,'bin','RL8-x86_64','labpva'));  % MEX + help stubs + libmpvaglue.so
addpath(fullfile(labpvaRoot,'doc'));                        % printpvs / printvals
% so the loader finds the EPICS shared libs at runtime (rpath is also set):
setenv('LD_LIBRARY_PATH', ['/usr/local/epics/R7.0.7/base/lib/RL8-x86_64:' ...
                           getenv('LD_LIBRARY_PATH')])

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

After **rebuilding** the MEX, run `clear mex` in any open MATLAB session (or
restart) so the cached old MEX are dropped and the new ones + `libmpvaglue.so`
load.

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

All 26 MEX and the glue layer build cleanly against EPICS 7.0.7 and MATLAB
R2023b, and labpva is **verified working live (2026-06-11)** against a softIocPVA
(`mdach:circle`): reads, `pvaGetStructure`, monitors (`pvaSetMonitor` →
`pvaNewMonitorValue` → cache-served read), the metadata getters, NTEnum reads,
and the `help` system all confirmed. The bring-up fixed a build bug where
process-global state (monitor registry, provider, timeout) was duplicated per
MEX, and a code review fixed monitor-cache leakage into metadata reads plus
smaller correctness issues — see [CHANGELOG.md](CHANGELOG.md). **Not yet
exercised live:** the write path (`pvaPut`/`pvaPutNoWait`/`pvaPutStructure`) and
array/string round-trips. Known limitations/follow-ups are at the end of
[ARCHITECTURE.md](ARCHITECTURE.md).
