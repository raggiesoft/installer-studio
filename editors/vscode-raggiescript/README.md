# RaggieScript & RaggieSoft Studio Language Support for VS Code

Official Visual Studio Code (and VS Code-compatible IDE) language extension for **RaggieScript (`.rssc`)** and **RaggieSoft Studio manifests (`.rsip`, `.rsis`, `.rsii`, `.rssp`, `.rstp`, `.rshp`)**.

## Features

1. **Full RaggieScript (`.rssc`) Syntax Highlighting**:
   - Preprocessor directives (`#if`, `#ifdef`, `#ifndef`, `#elif`, `#else`, `#endif`, `#define`, `include`, `use`)
   - Lifecycle & declaration blocks (`package`, `showcase`, `app`, `feature`, `param`, `payload`, `wizard`, `step`, `install`, `repair`, `uninstall`, `on`, `fn`)
   - String interpolation (`"Installing ${title}..."`), raw Windows path/registry strings (`r"SOFTWARE\RaggieSoft"`), and semantic version literals (`v"10.0.22000"`)
   - All built-in Win32, MSI SQL (`msi.*`), Foreign Function Interface (`ffi.*`), Restart Manager (`proc.*`), and TaskDialog (`ui.*`) namespaces
2. **Live Compile-Time Diagnostics (`RSW700` / `RSW701` / `RSW702`)**:
   - Automatically runs `instbuild.exe --validate --diagnostics=json` on save to flag syntax issues, `RSW701` Wizard Phase Isolation violations, and `RSW702` Capability Sandbox violations inline.
3. **Hover Documentation, Member IntelliSense, & Snippets**:
   - Type `msi.`, `reg.`, `fs.`, `proc.`, `ffi.`, or `ui.` for instant method completions, or use `rssc-package`, `rssc-taskdialog`, `rssc-msisql`, and `rssc-ffi` snippets.
4. **Integrated Build & MSI Prism Commands**:
   - `RaggieScript: Check Script (RSW701 & RSW702 Static Verifier)`
   - `RaggieScript: Compile Setup Executable (instbuild.exe)`
   - `RaggieScript: Disassemble RSWBC1 Bytecode`
   - `RaggieSoft: Open MSI Prism SQL Table Editor`

## License & Disclaimer

Released under the **MIT License** (`Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft`).

*Fictitious Persons & Entities Disclaimer: All narrative characters, bands, universities, and corporations referenced across RaggieSoft creative works are fictitious.*
