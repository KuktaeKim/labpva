# labpva — worked examples

Copy-paste-able MATLAB snippets for the common labpva tasks: reading scalars,
the labca-style type letter, monitoring (non-blocking poll **and** blocking wait
with timeout), reading/printing whole structures, and writing values back —
including writing a PV that is a structure.

> **PV names are illustrative — substitute your own.** `mdach:circle` is the
> structured test PV used during bring-up: its `value` is itself a sub-structure
> `{angle, x, y}`. `mdach:ao` stands for a plain NTScalar, `mdach:mbbo` for an
> NTEnum, `mdach:wf` for a waveform.
>
> Assumes labpva is on the MATLAB path (see README / `startup.m`) so the `pva*`
> verbs and the `printvals` / `printpvs` helpers resolve. After a rebuild, run
> `clear mex` first.

---

## 1. Reading scalar values

```matlab
v = pvaGet('mdach:ao');                 % NTScalar -> the .value (drop-in for lcaGet)

% Second output is the timestamp as a complex double: real=seconds, imag=nanos.
[v, ts] = pvaGet('mdach:ao');
when = datetime(real(ts),'ConvertFrom','posixtime') + seconds(imag(ts)/1e9);
fprintf('%s = %g  @ %s\n', 'mdach:ao', v, char(when));

% A cell of names returns an N-by-1 column (numeric if all are real scalars):
vals = pvaGet({'mdach:ao','mdach:ao2','mdach:ao3'});
```

---

## 2. The type letter (labca-style), including `'D'`

The optional 2nd argument is the labca type letter `N B S L F D C`. In pvAccess
the numeric widths all marshal to `double`, so the letters that actually change
the result are: `N` (native, default), the numeric letters (force a *numeric*
result), and `C` (force a *string*). The clearest effect is on an **NTEnum**.

```matlab
choice = pvaGet('mdach:mbbo');          % native  -> the selected choice STRING, e.g. 'Open'
idx    = pvaGet('mdach:mbbo', 'D');     % numeric -> the enum INDEX, e.g. 1
label  = pvaGet('mdach:mbbo', 'C');     % string  -> the choice string (same as native here)

% For an ordinary numeric PV, 'D' just means double (== native):
x = pvaGet('mdach:ao', 'D');            % 1x1 double
```

---

## 3. Monitoring — subscribe once, NON-blocking poll loop

`pvaSetMonitor` subscribes; `pvaNewMonitorValue` returns true once per newly
arrived sample (non-blocking). While a monitor is active, `pvaGet` is served
from the **monitor cache** — no network round-trip.

```matlab
pvaSetMonitor('mdach:ao');              % subscribe ONCE, before the loop
for k = 1:200
    if pvaNewMonitorValue('mdach:ao')   % true only when a new sample arrived
        v = pvaGet('mdach:ao');         % cache-served (the sample just reported)
        fprintf('update %d: %g\n', k, v);
    end
    pause(0.05);                        % yield; don't busy-spin
end

% Need a guaranteed fresh server read while the monitor stays up? Use poll=true:
vFresh = pvaGet('mdach:ao', true);      % bypass the cache, one fresh read
vFresh = pvaGet('mdach:ao', 'D', true); % type letter + poll together

pvaClear('mdach:ao');                   % unsubscribe (the connection stays cached)
```

---

## 4. Monitoring — BLOCKING wait on change, with a timeout

`pvaNewMonitorWait(pv, timeout)` blocks up to `timeout` seconds for the next
update. It returns `true` when a new sample arrived, `false` on timeout — ideal
for an event-driven loop that should not spin.

```matlab
pvaSetMonitor('mdach:ao');
tStart = tic;
while toc(tStart) < 30                          % run for ~30 s
    if pvaNewMonitorWait('mdach:ao', 5)         % block up to 5 s for the next update
        v = pvaGet('mdach:ao');                 % cache-served (just-arrived sample)
        fprintf('changed -> %g\n', v);
    else
        fprintf('no update in the last 5 s (timeout)\n');
    end
end
pvaClear('mdach:ao');
```

---

## 5. Reading a WHOLE structure, and printing just the values

`pvaGetStructure` returns the entire PVStructure as a nested MATLAB struct. Use
the bundled helpers to print it:
- `printvals(s, name)` — just each signal's `.value` + a readable timestamp
- `printpvs(s, name)`  — **every** leaf (value, alarm, timeStamp, display, control, …)

```matlab
s = pvaGetStructure('mdach:circle');    % whole tree as a nested struct
s.value.x                               % normal dot access to any field

printvals(s, 'mdach:circle');           % concise: values + timestamps only
printpvs(s, 'mdach:circle');            % verbose: every metadata leaf too

pvaInfo('mdach:circle')                 % type id + raw (un-sanitised) field tree
```

---

## 6. Monitoring a structure — print values on each change

`pvaGetStructure` is **cache-served by default** (like `pvaGet`) while a monitor
is active *and* the monitor's request covers what you ask for. Subscribe with
`'field()'` to monitor the whole structure, then print only the values:

```matlab
pvaSetMonitor('mdach:circle', 'field()');       % 'field()' = monitor the whole structure
tStart = tic;
while toc(tStart) < 30
    if pvaNewMonitorWait('mdach:circle', 5)      % block up to 5 s for the next update
        s = pvaGetStructure('mdach:circle');     % cache-served (monitor covers field())
        printvals(s, 'mdach:circle');            % print just the values
    end
end
pvaClear('mdach:circle');
```

Lighter alternative — monitor only what you print, and force a fresh read when
you really want one:

```matlab
req = 'field(value,timeStamp)';                  % smaller updates from the IOC
pvaSetMonitor('mdach:circle', req);
for k = 1:200
    if pvaNewMonitorValue('mdach:circle')
        % match the request so the cache covers it (otherwise it reads fresh):
        printvals(pvaGetStructure('mdach:circle', req), 'mdach:circle');
    end
    pause(0.05);
end
sFresh = pvaGetStructure('mdach:circle', true);  % poll=true: force a full fresh read
pvaClear('mdach:circle');
```

---

## 7. Writing a scalar value

```matlab
pvaPut('mdach:ao', 1.25);               % write and WAIT for server completion (cf. lcaPut)
pvaPutNoWait('mdach:ao', 1.25);         % fire-and-forget (no completion wait)

pvaPut('mdach:mbbo', 'Open');           % NTEnum: a string is matched to the choice list
pvaPut('mdach:mbbo', 1);                % NTEnum: a number sets the index
```

---

## 8. Writing a PV that is a STRUCTURE

`pvaPutStructure(pv, s [,request])` writes a whole structure. The rule:
**only the fields present in `s` are written** (matched by field name); any field
you leave out keeps its current server value. So the cleanest way to set a few
leaves is to build a *minimal* struct with just those fields.

```matlab
% --- set value.x and value.y on the structured PV, leave everything else alone ---
clear s
s.value.x = 1.5;
s.value.y = -2.0;
pvaPutStructure('mdach:circle', s);     % writes value.x / value.y; other fields unchanged

% confirm:
printvals(pvaGetStructure('mdach:circle', true), 'mdach:circle');   % poll=true for a fresh read
```

Read–modify–write of the full tree also works (handy when you want to tweak a
value relative to the current one). Scope the put to `value` so you don't write
back read-only metadata like `timeStamp`/`display`:

```matlab
s = pvaGetStructure('mdach:circle', true);   % fetch fresh
s.value.x = s.value.x + 0.1;                 % nudge x
pvaPutStructure('mdach:circle', s, 'field(value)');   % only the value sub-tree is written
```

`pvaPut` can also write a structured `value` directly (it targets the `.value`
field), which is convenient for an NTScalar whose value is itself a structure:

```matlab
clear v
v.x = 1.5;  v.y = -2.0;  v.angle = pi/4;
pvaPut('mdach:circle', v);               % writes the .value sub-structure
```

> Field-name note: PV field names that aren't legal MATLAB identifiers get
> sanitised on the way out (e.g. non-alphanumerics → `_`); on write-back the
> target structure's own field list is authoritative, so round-tripping a struct
> you got from `pvaGetStructure` is name-safe. `pvaInfo` shows the originals.

---

## 9. Cleanup

```matlab
pvaClear('mdach:ao');     % drop one monitor (channel/connection stays cached)
pvaClear();               % drop ALL monitors
```

After a monitor is cleared, `pvaGet` / `pvaGetStructure` on that name read fresh
from the server again (there is no cache to serve).
```
```
