/* pvaGetStatus.cpp - alarm severity/status and timestamp (cf. lcaGetStatus).
 *
 *   [sev, sta]      = pvaGetStatus(pvname[s])
 *   [sev, sta, ts]  = pvaGetStatus(pvname[s])    ts = sec + i*nsec (complex)
 *
 * severity/status are the EPICS alarm codes (0=NO_ALARM,1=MINOR,2=MAJOR,
 * 3=INVALID), read from the NT `alarm` field; returned as double here.
 */
#include "pvaMetaShared.h"
#include "pvaConvert.h"
using namespace labpva;
using namespace epics::pvData;
void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    if (nrhs < 1)
        mexErrMsgIdAndTxt("labpva:invalidArg", "pvaGetStatus: need a PV name");
    PvaError err;
    bool wasCell = false;
    std::vector<std::string> pvs = buildPVs(prhs[0], wasCell, err);
    errCheck(err);

    std::vector<double> sev, sta, secs, nsecs;
    bool wantTs = (nlhs >= 3);
    for (size_t i = 0; i < pvs.size(); ++i) {
        PVStructurePtr pv = pvaGet(pvs[i], "field(value,alarm,timeStamp)", err);
        if (err.err != PVA_OK) break;
        sev.push_back(getDoubleField(pv, "alarm.severity", 0.0));
        sta.push_back(getDoubleField(pv, "alarm.status", 0.0));
        if (wantTs) {
            double s = 0, ns = 0;
            pvTimeStampSecNsec(pv, s, ns);
            secs.push_back(s); nsecs.push_back(ns);
        }
    }
    errCheck(err);

    plhs[0] = colFromDoubles(sev, wasCell);
    if (nlhs >= 2) plhs[1] = colFromDoubles(sta, wasCell);
    if (wantTs)    plhs[2] = complexColumn(secs, nsecs);
}
