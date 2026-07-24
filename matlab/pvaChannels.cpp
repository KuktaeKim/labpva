/* pvaChannels.cpp - list the PV names labpva has opened a channel for.
 *
 *   names = pvaChannels()      N x 1 cell of char (0 x 1 cell if none)
 *
 * Read-only: reports the channels labpva has connected this session (any PV
 * touched by pvaGet/pvaPut/pvaSetMonitor/... - the connection persists and is
 * reused). This is a superset of pvaMonitors (a monitored PV is also connected,
 * but a plain pvaGet connects without monitoring). No side effects. Use
 * pvaIsConnected to test live connection state. No labca analogue.
 */
#include "mglue.h"
#include "pvaGlue.h"

using namespace labpva;

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    (void)nlhs; (void)nrhs; (void)prhs;
    std::vector<std::string> names = pvaChannelNames();
    mxArray *cell = mxCreateCellMatrix(names.size(), 1);
    for (size_t i = 0; i < names.size(); ++i)
        mxSetCell(cell, i, mxCreateString(names[i].c_str()));
    plhs[0] = cell;
}
