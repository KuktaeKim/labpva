function printvals(s, prefix)
%PRINTVALS  Print only .value (+ timeStamp) of each signal in a labpva structure.
%
%   s = pvaGetStructure('mdach:circle');
%   printvals(s, 'mdach:circle')
%
% Companion to PRINTPVS, which prints every leaf (alarm/display/control/...).
% PRINTVALS prints one line per *signal* -- a sub-structure that has a scalar
% .value field -- showing its value and, if present, a human-readable
% timeStamp. Nested non-signal sub-structures are recursed into so grouped
% signals (e.g. mdach:circle's angle/x/y) are all found.

    if nargin < 2, prefix = ''; end
    if ~isstruct(s), return; end

    f = fieldnames(s);
    for i = 1:numel(f)
        if isempty(prefix), name = f{i}; else, name = [prefix '.' f{i}]; end
        fld = s.(f{i});
        if ~isstruct(fld), continue; end

        if isfield(fld, 'value') && ~isstruct(fld.value)
            v = num2str(fld.value);
            if isfield(fld, 'timeStamp') && isfield(fld.timeStamp, 'secondsPastEpoch')
                ts = fld.timeStamp;
                when = datetime(double(ts.secondsPastEpoch) + double(ts.nanoseconds)/1e9, ...
                                'ConvertFrom', 'posixtime', ...
                                'TimeZone', 'America/Los_Angeles');
                fprintf('%-24s = %-12s  @ %s\n', name, v, char(when, 'yyyy-MM-dd HH:mm:ss.SSS'));
            else
                fprintf('%-24s = %s\n', name, v);
            end
        else
            printvals(fld, name);   % recurse into non-signal sub-structures
        end
    end
end
