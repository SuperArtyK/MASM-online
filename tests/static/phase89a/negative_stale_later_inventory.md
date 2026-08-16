# Correct opening followed by stale text

The current implemented Irvine32 output set includes direct `CALL Crlf`, zero-argument `INVOKE Crlf`, direct `CALL WriteChar`, and direct `CALL WriteString`, and direct `CALL WriteDec`.

Later in this active file, the currently supported Irvine32 output routines are **WriteChar** and `Crlf`.
