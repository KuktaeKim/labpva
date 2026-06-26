function printpvs(s, prefix)
%PRINTPVS  Recursively print every leaf field of a labpva structure.
%
%   s = pvaGetStructure('mdach:circle');
%   printpvs(s, 'mdach:circle')
%
% Walks the nested struct returned by pvaGetStructure / pvaGet and prints one
% line per leaf: "<dotted.path> = <value>". Nested structs, struct arrays and
% cell (string) arrays are handled.

    if nargin < 2, prefix = ''; end

    if isstruct(s)
        if numel(s) > 1                       % struct array (PV structureArray)
            for k = 1:numel(s)
                printpvs(s(k), sprintf('%s(%d)', prefix, k));
            end
            return;
        end
        f = fieldnames(s);
        for i = 1:numel(f)
            if isempty(prefix), p = f{i}; else, p = [prefix '.' f{i}]; end
            printpvs(s.(f{i}), p);
        end
    else
        fprintf('%-44s = %s\n', prefix, local_val2str(s));
    end
end

function str = local_val2str(v)
    if ischar(v)
        str = v;
    elseif islogical(v)
        str = mat2str(v);
    elseif isnumeric(v)
        if isscalar(v)
            str = num2str(v);
        elseif numel(v) <= 20
            str = mat2str(v);
        else
            str = sprintf('[%dx%d %s] %s ...', size(v,1), size(v,2), class(v), ...
                          mat2str(v(1:5)));
        end
    elseif iscell(v)
        try
            str = ['{' strjoin(cellfun(@(x) char(string(x)), v(:).', 'uni', 0), ', ') '}'];
        catch
            str = sprintf('{%dx%d cell}', size(v,1), size(v,2));
        end
    else
        str = sprintf('<%s>', class(v));
    end
end
