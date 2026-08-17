# Direct CALL and INVOKE remain distinct

Direct `CALL WriteString`, direct `CALL WriteDec`, direct `CALL WriteInt`, direct `CALL WriteHex`, and direct `CALL WriteBin` are implemented and executable. `INVOKE WriteString`, `INVOKE WriteDec`, `INVOKE WriteInt`, `INVOKE WriteHex`, and `INVOKE WriteBin` remain deferred until their owning future phases.
