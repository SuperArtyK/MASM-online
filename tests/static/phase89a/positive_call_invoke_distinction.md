# Exact source-form distinction

Direct `CALL WriteString`, direct `CALL WriteDec`, direct `CALL WriteInt`, and direct `CALL WriteHex` are implemented and executable. `INVOKE WriteString`, `INVOKE WriteDec`, `INVOKE WriteInt`, and `INVOKE WriteHex` remain deferred until their owning future phases.
