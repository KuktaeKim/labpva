function r = pvaBenchmarkPut(pv, n, value)
%PVABENCHMARKPUT  Time labpva write operations (for backend A/B comparison).
%
%   r = pvaBenchmarkPut(pvName)              % default n = 1000 iterations
%   r = pvaBenchmarkPut(pvName, n)
%   r = pvaBenchmarkPut({pv1;pv2;pv3}, n)    % one pvaPut call per iteration
%   r = pvaBenchmarkPut(pvName, n, value)    % value written every iteration
%
% Times n blocking pvaPut calls, n pvaPutNoWait calls, and (for reference) a
% put immediately after pvaClear. Prints us/put and returns a struct with the
% numbers.
%
% WRITES TO THE PV. Point it at a scratch record, not at live machine hardware.
%
% Intended use: run it on the SAME PV from the same host against each backend
% (PVXS in configure/RELEASE or not) and compare the warm rows. Restart MATLAB
% between builds (labpva is mexLock'd). A warm put costs ONE server round trip
% on both backends.
%
% Read the put-after-pvaClear row per backend, it measures DIFFERENT things:
% on PVXS, pvaClear retires the warm put operation, so the row is rebuild
% (INIT, and for enums one read) + put; on the classic backend pvaClear does
% NOT evict pvaClient's cached put handle, so the row is essentially another
% warm put. Neither equals what a put cost before warm channels existed.
%
%   r = pvaBenchmarkPut('labpva:test:setpoint');
%   r = pvaBenchmarkPut({'labpva:bench:1';'labpva:bench:2';'labpva:bench:3'});
%
% See also pvaPut, pvaPutNoWait, pvaBenchmark.

    if nargin < 2 || isempty(n), n = 1000; end
    if iscell(pv), npv = numel(pv); else, npv = 1; end
    if nargin < 3 || isempty(value), value = zeros(npv, 1); end

    pvaPut(pv, value);                  % connect + warm the put channel first

    t = tic;
    for k = 1:n, pvaPut(pv, value); end             %#ok<*NASGU>
    r.put_us = 1e6 * toc(t) / (n * npv);

    t = tic;
    for k = 1:n, pvaPutNoWait(pv, value); end
    r.putnowait_us = 1e6 * toc(t) / (n * npv);

    % Put right after pvaClear. NOT comparable across backends -- see the help
    % text: on PVXS this pays the channel rebuild, on the classic backend
    % pvaClear leaves pvaClient's put cache alone so this is ~a warm put.
    ncold = max(1, round(n / 10));
    t = tic;
    for k = 1:ncold
        if iscell(pv)
            for j = 1:npv, pvaClear(pv{j}); end
        else
            pvaClear(pv);
        end
        pvaPut(pv, value);
    end
    r.put_after_clear_us = 1e6 * toc(t) / (ncold * npv);

    r.pv = pv;
    r.n  = n;
    r.npv = npv;
    if iscell(pv), label = sprintf('%d PVs', npv); else, label = pv; end
    fprintf('pvaBenchmarkPut %s (n=%d, %d puts/iter):\n', label, n, npv);
    fprintf('  pvaPut (warm channel)    : %8.1f us/put\n', r.put_us);
    fprintf('  pvaPutNoWait             : %8.1f us/put\n', r.putnowait_us);
    fprintf('  pvaPut after pvaClear    : %8.1f us/put   (see help: differs by backend)\n', ...
            r.put_after_clear_us);
end
