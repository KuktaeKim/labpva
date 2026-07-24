function [img, info] = pvaGetImage(src, poll)
%PVAGETIMAGE  Read an EPICS NTNDArray (areaDetector image) as a shaped array.
%
%   img         = pvaGetImage(pvName)
%   img         = pvaGetImage(pvName, poll)   % poll=true forces a fresh read
%   img         = pvaGetImage(s)              % s = a struct from pvaGetStructure
%   [img, info] = pvaGetImage(...)
%
% Fetches the NTNDArray (via PVAGETSTRUCTURE) and returns a MATLAB image ready
% for imagesc/imshow:
%   * mono  -> 2-D  [ny x nx]       (rows = y, cols = x)
%   * color -> 3-D  [ny x nx x 3]   (RGB1/RGB2/RGB3, from the ColorMode attribute
%                                    or inferred from the size-3 dimension)
% Pixels come back as double.
%
% Compression (codec.name):
%   * ''      uncompressed -> reshaped by dimension[]
%   * 'jpeg'  decoded with imread (via a temp file); imread returns the shaped
%             image directly, so dimension[] is not needed for the reshape
%   * 'blosc' / 'lz4' / 'bslz4'  -> ERROR: these lossless codecs need a native
%             decoder. Decompress in the IOC (NDPluginCodec in decompress mode),
%             or add a decode MEX linking libblosc/liblz4.
%
% INFO (2nd output): .dims (NDArray dimension sizes, x-major as sent), .offset
% (per-dimension ROI offsets), .x / .y (pixel-CENTRE coordinate axis vectors --
% pixel i spans [offset+i, offset+i+1], so imagesc(info.x, info.y, img) shows the
% axes running exactly offset .. offset+size, i.e. the ROI's real position in the
% full sensor), .colorMode (numeric NDColorMode or NaN), .codec, .uniqueId, and
% .timeStamp (a datetime).
%
% Examples:
%   % simple display (axes are just pixel index 1..N):
%   img = pvaGetImage('BL31b:Pva1:Image');
%   figure; imagesc(img); axis image; colormap(gray)
%
%   % positioned at the ROI's real full-sensor coordinates -- REQUIRES the 2nd
%   % output, so use the [img, info] brackets (info.x/info.y carry the offset):
%   [img, info] = pvaGetImage('BL31b:Pva1:Image');
%   figure; imagesc(info.x, info.y, img); axis image; colormap(gray); colorbar
%   xlabel('x (sensor pixels)'); ylabel('y (sensor pixels)')
%
% See also pvaGetStructure, pvaGet, printvals, printpvs.

    if nargin < 2 || isempty(poll), poll = false; end

    % --- obtain the NTNDArray structure --------------------------------------
    if ischar(src) || isstring(src)
        s = pvaGetStructure(char(src), logical(poll));   % poll fwd: true=fresh read
    elseif isstruct(src)
        s = src;
    else
        error('pvaGetImage:badInput', ...
              'pvaGetImage: first arg must be a PV name or a pvaGetStructure struct');
    end

    if ~isstruct(s) || ~isfield(s, 'value') || ~isfield(s, 'dimension')
        error('pvaGetImage:notNTNDArray', ...
              'pvaGetImage: not an NTNDArray (no value/dimension fields)');
    end

    dims  = double([s.dimension.size]);      % x-major sizes, as sent
    cmode = local_colorMode(s);              % 0 Mono 1 Bayer 2 RGB1 3 RGB2 4 RGB3; NaN if absent

    codecName = '';
    if isfield(s, 'codec') && isstruct(s.codec) && isfield(s.codec, 'name')
        codecName = char(s.codec.name);
    end

    if isempty(codecName)
        % ---- uncompressed: reshape the raw pixels by dimension[] ------------
        img = local_reshape(double(s.value(:)).', dims, cmode);
    elseif strcmpi(codecName, 'jpeg')
        % ---- JPEG: decode the byte buffer with imread -----------------------
        img = double(local_decodeJpeg(s.value));   % imread returns [ny nx] / [ny nx 3]
    else
        error('pvaGetImage:compressed', ...
              ['pvaGetImage: codec ''%s'' not supported (only uncompressed and ' ...
               'jpeg). For blosc/lz4/bslz4, decompress in the IOC (NDPluginCodec ' ...
               'in decompress mode), or add a native decode MEX.'], codecName);
    end

    if nargout >= 2
        if isfield(s.dimension, 'offset')
            offs = double([s.dimension.offset]);
        else
            offs = zeros(1, numel(dims));
        end
        [ax, ay] = local_axes(dims, offs, cmode);   % pixel-coord axis vectors
        info = struct('dims', dims, 'offset', offs, 'colorMode', cmode, ...
                      'codec', codecName, 'x', ax, 'y', ay);
        if isfield(s, 'uniqueId'), info.uniqueId = s.uniqueId; end
        info.timeStamp = local_ts(s);
    end
end

% ---------------------------------------------------------------------------
function img = local_reshape(px, dims, cmode)
%LOCAL_RESHAPE  reshape a flat (x-major) pixel vector into a MATLAB image.
    n = numel(dims);
    if isempty(dims) || prod(dims) ~= numel(px)
        error('pvaGetImage:shape', ...
              'pvaGetImage: pixel count %d != dimension product %d', ...
              numel(px), prod(dims));
    end
    if ~isnan(cmode) && ~ismember(cmode, [0 1 2 3 4])
        error('pvaGetImage:colorMode', ...
              'pvaGetImage: ColorMode %d not supported (only Mono/Bayer/RGB)', cmode);
    end

    isColor = ismember(cmode, [2 3 4]) || (isnan(cmode) && n == 3);
    if isColor
        if n ~= 3
            error('pvaGetImage:color', ...
                  'pvaGetImage: colour image expected 3 dimensions, got %d', n);
        end
        A = reshape(px, dims);               % as sent
        switch cmode
            case 2, img = permute(A, [3 2 1]);   % RGB1 [3 nx ny] -> [ny nx 3]
            case 3, img = permute(A, [3 1 2]);   % RGB2 [nx 3 ny] -> [ny nx 3]
            case 4, img = permute(A, [2 1 3]);   % RGB3 [nx ny 3] -> [ny nx 3]
            otherwise                            % inferred: which axis is size 3
                c = find(dims == 3, 1);
                if isempty(c)
                    error('pvaGetImage:color', ...
                          'pvaGetImage: 3-D image but no size-3 colour dimension');
                end
                order = {[3 2 1], [3 1 2], [2 1 3]};
                img = permute(A, order{c});
        end
    else                                     % mono / Bayer -> 2-D
        if n ~= 2
            error('pvaGetImage:shape', ...
                  'pvaGetImage: mono image expected 2 dimensions, got %d', n);
        end
        img = permute(reshape(px, dims), [2 1]);   % [nx ny] -> [ny nx]
    end
end

% ---------------------------------------------------------------------------
function [x, y] = local_axes(dims, offs, cmode)
%LOCAL_AXES  pixel-coordinate axis vectors x,y = offset + (0:size-1), picking the
%   spatial (non-colour) dimensions so imagesc(x,y,img) is placed correctly.
    n = numel(dims);
    if numel(offs) < n, offs(end+1:n) = 0; end
    isColor = ismember(cmode, [2 3 4]) || (isnan(cmode) && n == 3);
    if ~isColor
        xi = 1; yi = 2;                      % mono: dims = [nx ny]
    else
        switch cmode
            case 2, xi = 2; yi = 3;          % RGB1 [3 nx ny]
            case 3, xi = 1; yi = 3;          % RGB2 [nx 3 ny]
            case 4, xi = 1; yi = 2;          % RGB3 [nx ny 3]
            otherwise                        % inferred: the two non-size-3 dims
                sp = setdiff(1:n, find(dims == 3, 1));
                xi = sp(1); yi = sp(2);
        end
    end
    % pixel CENTRES at offset+0.5, offset+1.5, ... so the displayed image occupies
    % exactly [offset, offset+size] (imagesc uses x(1)/x(end) as the end centres).
    x = offs(xi) + 0.5 + (0:dims(xi)-1);
    y = offs(yi) + 0.5 + (0:dims(yi)-1);
end

% ---------------------------------------------------------------------------
function img = local_decodeJpeg(value)
%LOCAL_DECODEJPEG  decode a JPEG byte buffer via a temp file + imread.
    % value is the compressed byte buffer marshalled to double; map to raw bytes
    % (mod 256 handles a signed-byte union too, e.g. -1 -> 255).
    bytes = uint8(mod(round(double(value(:))), 256));
    fn  = [tempname '.jpg'];
    fid = fopen(fn, 'wb');
    if fid < 0
        error('pvaGetImage:tmp', 'pvaGetImage: cannot open temp file %s', fn);
    end
    fwrite(fid, bytes, 'uint8');
    fclose(fid);
    cleanup = onCleanup(@() delete(fn));   %#ok<NASGU>  remove temp file on exit
    img = imread(fn);                       % [ny nx] (uint8) or [ny nx 3]
end

% ---------------------------------------------------------------------------
function cmode = local_colorMode(s)
%LOCAL_COLORMODE  numeric NDColorMode from the attribute array, or NaN.
    cmode = NaN;
    if ~isfield(s, 'attribute') || ~isstruct(s.attribute), return; end
    a = s.attribute;
    for i = 1:numel(a)
        if isfield(a(i), 'name') && strcmp(char(a(i).name), 'ColorMode') ...
                                 && isfield(a(i), 'value')
            v = a(i).value;
            if isnumeric(v) && ~isempty(v), cmode = double(v(1)); end
            return;
        end
    end
end

% ---------------------------------------------------------------------------
function when = local_ts(s)
%LOCAL_TS  datetime from dataTimeStamp (falling back to timeStamp), or NaT.
    when = NaT;
    if isfield(s, 'dataTimeStamp') && isfield(s.dataTimeStamp, 'secondsPastEpoch')
        ts = s.dataTimeStamp;
    elseif isfield(s, 'timeStamp') && isfield(s.timeStamp, 'secondsPastEpoch')
        ts = s.timeStamp;
    else
        return;
    end
    when = datetime(double(ts.secondsPastEpoch) + double(ts.nanoseconds)/1e9, ...
                    'ConvertFrom', 'posixtime', 'TimeZone', 'America/Los_Angeles');
end
