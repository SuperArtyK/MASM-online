# Direct CALL and INVOKE remain distinct

Direct `CALL WriteString`, direct `CALL WriteDec`, direct `CALL WriteInt`, direct `CALL WriteHex`, direct `CALL WriteBin`, and direct `CALL DumpRegs` are implemented and executable. `INVOKE WriteString`, `INVOKE WriteDec`, `INVOKE WriteInt`, `INVOKE WriteHex`, `INVOKE WriteBin`, and `INVOKE DumpRegs` remain deferred until their owning future phases.
