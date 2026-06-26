/* pvaConvert.h - PVStructure <-> MATLAB mxArray marshalling for labpva
 *
 * This is the heart of labpva and the part with no analogue in labca: a
 * Channel Access PV carries a single typed value (scalar or waveform), but a
 * pvAccess channel carries an arbitrary PVStructure tree (NTScalar wraps the
 * payload in a `.value` field alongside `.alarm`, `.timeStamp`, `.display`,
 * ...; NTTable is a structure-of-columns; NTNDArray is an image; and a server
 * may expose entirely custom structures). These functions translate that tree
 * to and from native MATLAB types.
 *
 * Representation contract (see ARCHITECTURE.md for the full table):
 *   PV scalar (numeric)   -> 1x1 double          (int widths collapse to double,
 *                                                  matching labca's ergonomics)
 *   PV scalar (boolean)   -> 1x1 logical
 *   PV scalar (string)    -> char row vector
 *   PV scalarArray num    -> 1xN double row vector
 *   PV scalarArray string -> 1xN cell of char
 *   PV structure          -> 1x1 struct, one field per PV sub-field (recursive)
 *   PV structureArray     -> 1xN struct array (homogeneous fields)
 *   PV union              -> the stored field, unwrapped (or [] if empty)
 *   PV unionArray         -> 1xN cell array
 *   enum_t sub-structure  -> struct(index, choices{}, choice)  [NT-aware sugar]
 *
 * Field names that are not valid MATLAB identifiers are sanitised (see
 * mxFieldName); the original name is preserved verbatim on write-back via the
 * target PVStructure's own field list, so round-tripping is name-safe.
 */
#ifndef PVA_CONVERT_H
#define PVA_CONVERT_H

#include "mex.h"
#include "pvaError.h"
#include <pv/pvData.h>

namespace labpva {

/* ---- PV -> MATLAB ---------------------------------------------------- */

/* Convert any PVField to the MATLAB type given by the representation
 * contract above. Recurses through structures/arrays/unions. Never returns
 * null: an unrepresentable leaf becomes an empty [] and records PVA_UNSUPPORTED
 * in `err` (the traversal continues so a partial structure still comes back). */
mxArray *pvFieldToMx(const epics::pvData::PVFieldPtr &field, PvaError &err);

/* Top-level convenience: convert a whole PVStructure to a 1x1 MATLAB struct. */
mxArray *pvStructureToMx(const epics::pvData::PVStructurePtr &pv, PvaError &err);

/* Value-only extraction used by pvaGet (the labca-faithful path). Pulls the
 * "value" field out of an NTScalar/NTScalarArray/NTEnum so pvaGet behaves like
 * lcaGet; for a non-NT or value-less structure it falls back to the whole
 * structure via pvStructureToMx. `typeReq` mirrors labca's type letter
 * ('N','D','C',...): 'C' forces string/cell output, others request numeric.
 * Returns a freshly created mxArray. */
mxArray *pvValueToMx(const epics::pvData::PVStructurePtr &pv, char typeReq, PvaError &err);

/* Read the EPICS timestamp into seconds-past-epoch / nanoseconds (both 0 if
 * the structure has no timeStamp field). The MEX layer packs these into the
 * labca-style complex double (sec + i*nsec) via mglue's complexColumn, which
 * works under both the classic and interleaved MATLAB complex APIs. */
void pvTimeStampSecNsec(const epics::pvData::PVStructurePtr &pv,
                        double &secOut, double &nsecOut);

/* Marshal the alarm sub-structure to struct(severity,status,message), or an
 * all-zero struct if absent. */
mxArray *pvAlarmToMx(const epics::pvData::PVStructurePtr &pv);

/* ---- MATLAB -> PV ---------------------------------------------------- */

/* Populate an existing target PVField from a MATLAB value, converting element
 * types as needed (the target's introspection is authoritative). Handles
 * scalars, scalar arrays, nested structs (matched by field name) and struct
 * arrays. Records the first conversion problem in `err` and keeps going where
 * it safely can. */
void mxToPvField(const mxArray *mx, const epics::pvData::PVFieldPtr &target, PvaError &err);

/* Convenience for pvaPut value-only writes: set just the `value` field of an
 * NTScalar/NTScalarArray (or the single value field of a plain structure)
 * from a MATLAB scalar/vector/string. Returns the bitset-relevant field name
 * actually written, or "" on failure. */
std::string mxToPvValue(const mxArray *mx, const epics::pvData::PVStructurePtr &pv, char typeReq, PvaError &err);

/* ---- helpers (exposed for the metadata MEX functions) ---------------- */

/* Sanitise a PV field name into a legal MATLAB struct field identifier. */
std::string mxFieldName(const std::string &pvName);

} // namespace labpva

#endif /* PVA_CONVERT_H */
