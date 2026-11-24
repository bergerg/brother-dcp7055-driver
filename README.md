# Brother DCP-7055 driver

Print-only macOS CUPS driver for the Brother DCP-7055, connected over USB.

## Future: CUPS driver deprecation

CUPS printer drivers and backends are deprecated and will stop working in a
future CUPS release. No removal date from Apple was found as of November 2026.

**Decision:** keep the current driver while it works. Convert it to a Printer
Application (a local driverless IPP printer) when Apple announces removal.

Untested plan:

1. **Queue:** run macOS's `ippeveprinter` on localhost with Bonjour advertising
   off (`-r off`), started by `launchd`. Create the queue with
   `lpadmin -m everywhere`, which is not deprecated.
2. **Filter:** adapt `rastertodcp7055.c` to read PWG raster. It accepts only
   1-bit pixels today, so 8-bit gray input would need dithering.
3. **USB:** send data to the printer with Apple's `IOUSBHost` framework, because
   the CUPS `usb` backend is deprecated too.

Stay in C to reuse the existing code. If Apple also removes `ippeveprinter`,
replace step 1 with PAPPL (C) or `ipp-printer-app` (Rust).

Trade-off: if a macOS upgrade removes drivers first, printing stops until the
conversion is done.
