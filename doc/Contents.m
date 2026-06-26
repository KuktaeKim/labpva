% labpva - EPICS pvAccess interface for MATLAB (a labca-style PVA binding)
%
% Read / write
%   pvaGet              - read a channel's value(s)        [val,ts] = pvaGet(pv[s][,type])
%   pvaGetStructure     - read the whole PVStructure as a nested struct
%   pvaPut              - write value(s), wait for completion
%   pvaPutNoWait        - write value(s), do not wait
%   pvaPutStructure     - write a whole structure from a MATLAB struct
%   pvaInfo             - introspect a channel (type id + field-tree dump)
%
% Monitors
%   pvaSetMonitor       - subscribe to value changes
%   pvaNewMonitorValue  - non-blocking: has a new value arrived?
%   pvaNewMonitorWait   - block until a new value arrives
%   pvaClear            - tear down monitors / cached channels
%
% Metadata
%   pvaGetStatus        - [severity, status, timestamp]
%   pvaGetNelem         - element count of the value field
%   pvaGetControlLimits - drive-range limits          (control.*)
%   pvaGetGraphicLimits - display-range limits         (display.*)
%   pvaGetAlarmLimits   - alarm thresholds             (valueAlarm.*)
%   pvaGetWarnLimits    - warning thresholds           (valueAlarm.*)
%   pvaGetUnits         - engineering units string
%   pvaGetPrecision     - display precision
%   pvaGetEnumStrings   - enum choice strings
%
% Configuration / diagnostics
%   pvaSetTimeout / pvaGetTimeout   - connect/IO timeout (seconds)
%   pvaSetProvider / pvaGetProvider - 'pva' (default) or 'ca'
%   pvaDebugOn / pvaDebugOff        - toggle debug output
%   pvaLastError                    - [code, message] of last operation
%
% Timestamps are complex doubles: real = seconds past epoch, imag = nanoseconds.
% See ARCHITECTURE.md for the labca->PVA mapping and the structure model.
