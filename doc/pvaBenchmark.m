function r = pvaBenchmark(pv, n)
%PVABENCHMARK  Time labpva read operations (for backend A/B comparison).
%
%   r = pvaBenchmark(pvName)        % default n = 200 iterations
%   r = pvaBenchmark(pvName, n)
%
% Times n fresh server reads (pvaGet with poll=true), n whole-structure reads,
% and n metadata reads, and prints ms/call. Returns a struct with the numbers.
%
% Intended use: build labpva once with the classic backend and once with PVXS
% (PVXS in configure/RELEASE), run this on the SAME PV from the same host in
% each build, and compare. Restart MATLAB between builds (labpva is mexLock'd).
%
%   r = pvaBenchmark('BL31b:Pva1:Image');       % image PV: marshalling-heavy
%   r = pvaBenchmark('labpva:test:ao');         % scalar PV: round-trip-heavy
%
% See also pvaGet, pvaGetStructure, pvaGetUnits.

    if nargin < 2 || isempty(n), n = 200; end

    pvaGet(pv, true);                   % connect + warm the channel first

    t = tic;
    for k = 1:n, pvaGet(pv, true); end          %#ok<*NASGU>
    r.get_ms = 1e3 * toc(t) / n;

    t = tic;
    for k = 1:n, s = pvaGetStructure(pv, true); end
    r.getstructure_ms = 1e3 * toc(t) / n;

    t = tic;
    for k = 1:n, u = pvaGetUnits(pv); end
    r.getunits_ms = 1e3 * toc(t) / n;

    r.pv = pv;
    r.n  = n;
    fprintf('pvaBenchmark %s (n=%d):\n', pv, n);
    fprintf('  pvaGet(pv,true)          : %8.3f ms/call\n', r.get_ms);
    fprintf('  pvaGetStructure(pv,true) : %8.3f ms/call\n', r.getstructure_ms);
    fprintf('  pvaGetUnits(pv)          : %8.3f ms/call\n', r.getunits_ms);
end
