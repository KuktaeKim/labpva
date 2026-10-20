/* pvaSetProvider.cpp - choose the per-channel provider (no labca analogue;
 * Channel Access has only one protocol).
 *
 *   pvaSetProvider('pva')      use pvAccess (default)
 *   pvaSetProvider('ca')       use Channel Access for subsequently-named channels
 *
 * The token is trimmed and case-insensitive ('' means 'pva'); anything else is
 * rejected here with labpva:invalidArg and the current provider is unchanged.
 * Affects channels opened after the call; already-cached channels keep their
 * provider. Both providers are always available to the underlying client.
 */
#include "mglue.h"
#include "pvaGlue.h"

#include <cctype>
#include <string>

using namespace labpva;
void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    (void)nlhs; (void)plhs;
    if (nrhs < 1 || !mxIsChar(prhs[0]))
        mexErrMsgIdAndTxt("labpva:invalidArg", "pvaSetProvider: need 'pva' or 'ca'");

    std::string arg = argString(prhs[0]);
    size_t b = 0, e = arg.size();
    while (b < e && std::isspace((unsigned char)arg[b]))     ++b;
    while (e > b && std::isspace((unsigned char)arg[e - 1])) --e;
    arg = arg.substr(b, e - b);

    std::string token = arg;
    for (size_t i = 0; i < token.size(); ++i)
        token[i] = (char)std::tolower((unsigned char)token[i]);
    if (token.empty()) token = "pva";
    if (token != "pva" && token != "ca")
        mexErrMsgIdAndTxt("labpva:invalidArg",
            "pvaSetProvider: unknown provider '%s' (use 'pva' or 'ca')", arg.c_str());

    /* The token is valid, so a refusal can only mean this backend lacks it. */
    if (!pvaSetProvider(token))
        mexErrMsgIdAndTxt("labpva:unsupported",
            "pvaSetProvider: this labpva build uses the PVXS backend, which "
            "speaks pvAccess only ('ca' unavailable; use labca to read "
            "record.FIELD channels over Channel Access)");
}
