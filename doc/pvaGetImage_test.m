%PVAGETIMAGE_TEST  Manual test procedure for pvaGetImage.
%
% Run the sections below one at a time (Ctrl-Enter on each %% block, or
% copy a block to the command window). Assumes labpva is on the MATLAB path
% (startup.m / README) and BL31b:Pva1:Image is reachable. Substitute your own
% PV names as needed.
%
% PASS CRITERIA (summary):
%   * Section 1 shows a clean (un-sheared) beam image of the expected size.
%   * Section 2 gives an EXACT match on the same frame.
%   * info.codec is '' (uncompressed); prod(info.dims) == numel(img).
%   * The non-image PV raises 'pvaGetImage:notNTNDArray'.
%   * MATLAB exits cleanly (no segfault) after use.
% If Section 1/2 look sheared or the size looks transposed, the width/height
% (x/y) order may need swapping for your camera -- report it.

IMG   = 'BL31b:Pva1:Image';        % an uncompressed NTNDArray (areaDetector image)
RESLT = 'BL31b:ImgProc1:Result';   % a custom Q:group (NOT an NTNDArray)


%% 0. Prep -- make MATLAB see the newly added pvaGetImage.m
rehash
which pvaGetImage                  % -> .../doc/pvaGetImage.m
help pvaGetImage                   % help text should load
% (no clear mex / restart needed -- pvaGetImage is a plain .m, not a MEX)


%% 1. Basic read + display (uncompressed path)
[img, info] = pvaGetImage(IMG);
disp(class(img))                   % expect 'double'
disp(size(img))                    % expect [ny nx], e.g. [300 300]
figure; imagesc(info.x, info.y, img); axis image; colormap(gray); colorbar
xlabel('x (sensor pixels)'); ylabel('y (sensor pixels)')
title(['pvaGetImage: ' IMG], 'Interpreter','none')
% axes span offset..offset+size-1 (the ROI's position in the full sensor)
% LOOK FOR: a sensible beam blob, correct aspect, NOT scrambled / diagonally
% sheared (shearing => wrong reshape/orientation).


%% 2. Correctness cross-check (same frame -> must match exactly)
b    = pvaGetStructure(IMG, true);            % poll=true: fresh, bypass cache
img1 = pvaGetImage(b);                         % feed the SAME struct
nx   = double(b.dimension(1).size);            % x (fastest / width)
ny   = double(b.dimension(2).size);            % y (height)
manual = reshape(double(b.value), [nx ny]).';  % [ny nx], rows = y
fprintf('size match : %d\n', isequal(size(img1), size(manual)));   % expect 1
fprintf('exact match: %d\n', isequal(img1, manual));                % expect 1
% NOTE: comparing two SEPARATE calls on a live camera can differ (different
% frames); that's why this feeds one struct to both. Size must always match.


%% 3. The info output (incl. offset-aware axes)
[img, info] = pvaGetImage(IMG);
disp(info)
fprintf('codec empty (uncompressed): %d\n', isempty(info.codec));         % expect 1
fprintf('pixel count == prod(dims) : %d\n', prod(info.dims)==numel(img)); % expect 1
fprintf('x axis extent: %g .. %g   (offset=%g, size=%g)\n', ...
        info.offset(1), info.offset(1)+info.dims(1), info.offset(1), info.dims(1));
fprintf('y axis extent: %g .. %g   (offset=%g, size=%g)\n', ...
        info.offset(2), info.offset(2)+info.dims(2), info.offset(2), info.dims(2));


%% 4. poll / monitor interaction
pvaSetMonitor(IMG, 'field()');
for k = 1:20
    if pvaNewMonitorValue(IMG)
        [img, info] = pvaGetImage(IMG);       % served from the monitor cache
        imagesc(info.x, info.y, img); axis image; drawnow
    end
    pause(0.05)
end
pvaClear(IMG);
imgFresh = pvaGetImage(IMG, true);            % poll=true -> guaranteed fresh read
disp(size(imgFresh))


%% 5. Error handling
% not an NTNDArray -> clear error, no crash:
try
    pvaGetImage(RESLT);
    disp('UNEXPECTED: no error');
catch e
    fprintf('got expected error id: %s\n', e.identifier);   % pvaGetImage:notNTNDArray
end
% (the 'pvaGetImage:compressed' path only triggers on a blosc/lz4/bslz4/ image;
%  it cannot be exercised on this uncompressed PV.)


%% 6. Regression checks (nothing else broke)
v = pvaGet(IMG);                    % smart pvaGet: expect a STRUCT, not a flat vector
fprintf('pvaGet(image) is struct : %d\n', isstruct(v));          % expect 1
a = pvaGet(RESLT);
fprintf('pvaGet(group) is struct : %d\n', isstruct(a));          % expect 1
% Then quit MATLAB normally and confirm NO segmentation fault on exit
% (that verifies the mexLock teardown fix still holds).


%% 7. OPTIONAL -- physical sanity cross-check vs ImgProc centroid/peak
[~, pk]  = max(img(:));
[py, px] = ind2sub(size(img), pk);            % 1-based peak (row=y, col=x)
r = pvaGet(RESLT);
fprintf('image peak (x,y) = (%g, %g)\n', px, py);
% Compare px/py to r.maxPosX.value / r.maxPosY.value (should be close).


%% 8. OPTIONAL -- JPEG path (only if you have a codec='jpeg' image PV)
% JPG = 'SOME:Jpeg:Image';
% jimg = pvaGetImage(JPG);
% figure; imagesc(jimg); axis image; colormap(gray)
% [~, jinfo] = pvaGetImage(JPG);  disp(jinfo.codec)   % expect 'jpeg'
