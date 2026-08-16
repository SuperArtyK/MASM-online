# Exact source-form distinction

Direct `CALL WriteString`, direct `CALL WriteDec`, and direct `CALL WriteInt` are implemented and executable. `INVOKE WriteString`, `INVOKE WriteDec`, and `INVOKE WriteInt` remain deferred until their owning future phases.
