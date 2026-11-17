# Mixed exact-form inventory

The current Irvine32 output set includes `Crlf`, direct `WriteChar`, direct `WriteString`, direct `WriteDec`, direct `WriteInt`, direct `WriteHex`, direct `WriteBin`, and direct `DumpRegs`, while `INVOKE WriteString`, `INVOKE WriteHex`, `INVOKE WriteBin`, and `INVOKE DumpRegs` remain deferred.
