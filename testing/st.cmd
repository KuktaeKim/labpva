#!/bin/sh
# st.cmd - start a pvAccess soft IOC serving labpvaTest.db.
#
# softIocPVA ships with EPICS 7 base. Either run this script (with base on your
# PATH) or invoke directly:
#
#     softIocPVA -d labpvaTest.db
#
# Then from MATLAB (labpva on the path):
#
#     pvaInfo('labpva:test:ao')
#     pvaGet('labpva:test:ao')
#     pvaPut('labpva:test:setpoint', 12.5)
#     pvaGetEnumStrings('labpva:test:enum')
#     s = pvaGetStructure('labpva:test:ao')

exec softIocPVA -d "$(dirname "$0")/labpvaTest.db"
