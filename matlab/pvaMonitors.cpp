/* pvaMonitors.cpp - list the PV names that currently have an active monitor.
 *
 *   names = pvaMonitors()      N x 1 cell of char (0 x 1 cell if none)
 *
 * Read-only: reports labpva's monitor registry -- channels subscribed via
 * pvaSetMonitor and not yet pvaClear'd. No side effects (does not touch the
 * monitor queues or the new-value flags). No labca analogue.
 */
#include "mglue.h"
#include "pvaGlue.h"

using namespace labpva;

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    (void)nlhs; (void)nrhs; (void)prhs;
    std::vector<std::string> names = pvaMonitorNames();
    mxArray *cell = mxCreateCellMatrix(names.size(), 1);
    for (size_t i = 0; i < names.size(); ++i)
        mxSetCell(cell, i, mxCreateString(names[i].c_str()));
    plhs[0] = cell;
}
