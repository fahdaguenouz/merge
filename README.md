# Merge — Linux ELF binary binder

`merge` combines two x86-64 Linux ELF executables into one executable file. When
the generated file is launched, it runs the first embedded program, waits for it
to finish, and then runs the second. This repository implements only the
mandatory project scope: one executable format and a fixed execution order. It
does not include the bonus multi-format support, configurable conditions/order,
or a GUI.

> This is an educational binary-analysis project. Use it only with programs you
> own or are authorized to modify and execute.

## Requirements

- A 64-bit x86 Linux system
- A C11 compiler (`cc`, GCC, or Clang)
- POSIX `make`
- `/proc` mounted (normal on Linux)

Both input programs must be regular, 64-bit, little-endian x86-64 ELF
executables. Dynamically linked PIE/non-PIE and statically linked ELF programs
are accepted. PE, Mach-O, scripts, 32-bit ELF, and other architectures are
intentionally rejected.

## Build and use

Build the binder and the two example programs:

```sh
make
```

Run the examples independently:

```console
$ ./bin1
Message from bin1
$ ./bin2
Message from bin2
```

Running the binder without arguments prints its usage:

```console
$ ./merge
Welcome to the merge program.
Usage: ./merge source-binary1 source-binary2 -o output-binary
```

Bind and run them:

```console
$ ./merge bin1 bin2 -o bin3
bin1 and bin2 merged into bin3 successfully!
$ ./bin3
Message from bin1
Message from bin2
```

Any arguments passed to the generated program are forwarded unchanged to both
embedded programs. The generated program always attempts to run both payloads,
even if the first fails. Its exit status is the second program's nonzero status,
if any; otherwise it is the first program's status.

Run the automated audit checks with:

```sh
make test
```

Remove generated files with `make clean`.

## Source-code architecture

The implementation is separated by responsibility so each part can be studied
and presented independently:

- `src/main.c` detects binder or runner mode and validates the command line.
- `src/format.c` validates ELF64 headers and parses bundle footer metadata.
- `src/binder.c` constructs the output file and records payload locations.
- `src/runner.c` extracts and executes both embedded programs sequentially.
- `src/io.c` provides reliable streaming read/write helpers.
- `include/merge.h` defines the shared footer structure and module interfaces.

`main.c` is the control-flow entry point. It delegates to either `binder.c` or
`runner.c`; both use `format.c` and `io.c` for their lower-level work. This keeps
ELF analysis separate from file construction and process execution.

## How the binder works

The same native executable has two modes:

1. **Binder mode.** A normal `merge` executable has no merge footer. It validates
   the two inputs, copies its own ELF image into a new output file, appends both
   input files unchanged, then appends a fixed metadata footer. The output is
   written to a temporary file and atomically renamed into place so a failed
   operation does not leave a partial result.
2. **Runner mode.** A generated executable finds the footer at its own end and
   checks its magic, version, sizes, offsets, and bounds. It copies each payload
   into an anonymous Linux `memfd`, forks, and executes that descriptor with
   `fexecve`. It waits for program one before starting program two. No payload is
   written as a named file on disk.

The resulting on-disk layout is:

```text
+----------------------+  offset 0
| merge runner ELF     |
+----------------------+  first_offset
| first ELF (unchanged)|
+----------------------+  second_offset
| second ELF (unchanged)|
+----------------------+
| merge_footer         |  fixed-size final record
+----------------------+  end of file
```

The footer contains an eight-byte magic value, format version, footer size,
64-bit offset and size pairs for both payloads, and their display names. Runtime
bounds checks ensure those regions are contiguous and finish immediately before
the footer.

## Step-by-step walkthrough

For `./merge bin1 bin2 -o bin3`, the program:

1. Opens each input read-only and verifies it is a regular file.
2. Reads its `Elf64_Ehdr` and verifies the ELF magic, 64-bit class,
   little-endian encoding, current ELF version, x86-64 machine type, and an
   executable or shared-object ELF type (`ET_EXEC` or `ET_DYN`). PIE executables
   use `ET_DYN`, which is why both types are accepted.
3. Opens `/proc/self/exe` to obtain the clean runner image.
4. Creates a mode-0700 temporary output next to the requested destination.
5. Streams the runner, `bin1`, `bin2`, and footer into that file using bounded
   buffers; it does not load whole binaries into memory.
6. Flushes and closes the file, then atomically renames it to `bin3`.
7. On `./bin3`, detects and validates the footer, loads each stored ELF into an
   anonymous in-memory file, and executes them sequentially in child processes.

## ELF64 structure and why appending works

An ELF file starts with an ELF header (`Elf64_Ehdr`). Important fields include:

- `e_ident`: magic bytes (`0x7f`, `E`, `L`, `F`), word size, byte order, and ABI
  identification.
- `e_type`: object type. Normal fixed-address executables are `ET_EXEC`; modern
  position-independent executables are commonly `ET_DYN`.
- `e_machine`: target instruction set; this implementation requires
  `EM_X86_64`.
- `e_entry`: the virtual address where execution begins.
- `e_phoff` / `e_phnum`: location and count of program-header entries.
- `e_shoff` / `e_shnum`: location and count of section-header entries.

Program headers describe segments the Linux loader maps into memory. Section
headers describe logical regions useful to linkers and analysis tools. At
execution, the kernel follows the runner's original program headers and entry
point. Bytes appended beyond the segments described by those headers are not
mapped as runner code, so the two payloads and footer can safely live there.

This binder deliberately does **not** rewrite either payload's ELF header or
entry point. The outer file retains the binder runner's header and entry point;
each embedded ELF remains byte-for-byte intact and is later handed back to the
kernel through `fexecve`, allowing the kernel and dynamic loader to honor that
payload's own program headers and `e_entry`. This avoids fragile relocation and
entry-point patching.

## Error handling and limitations

- Unsupported or truncated files are rejected before output creation.
- Existing output files are replaced only after a complete new bundle has been
  written successfully.
- The tool needs Linux `memfd_create`, `/proc/self/exe`, `fork`, and `fexecve`.
- The output is larger than the runner plus both source files and its footer.
- Interactive programs share the same terminal but run one at a time.
- Signals and process state are not merged; each payload is a separate child
  process. A signal termination maps to the conventional status `128 + signal`.
- Set-user-ID/set-group-ID file semantics and file capabilities from inputs are
  not preserved because payloads execute from anonymous descriptors.
- A bundle cannot itself be used as the binder command because it detects its
  footer and enters runner mode.

## Security, ethical, and legal report

Binary binders have legitimate uses in software packaging, compatibility
launchers, controlled experiments, incident-response training, and learning how
loaders interpret executable files. The same mechanism can also conceal an
unwanted payload, alter expected program behavior, bypass review processes, or
violate software licenses. Never bind, distribute, or run software without the
rights and informed authorization to do so. Laws differ by jurisdiction, and
license terms, copyright, anti-circumvention rules, computer-misuse laws, and
organizational policy may all apply. This documentation is not legal advice.

The generated file is not signed and provides no authenticity guarantee. For a
production defense against tampering:

- verify publisher signatures and cryptographic hashes before execution;
- use reproducible builds and protect signing keys outside build hosts;
- restrict write permissions on executables and deployment directories;
- enforce least privilege, mandatory access controls, and trusted deployment
  pipelines;
- scan and inventory artifacts, monitor unexpected hash/size changes, and retain
  audit logs;
- use measured boot or platform code-signing enforcement where appropriate;
- investigate appended data and compare ELF segment boundaries with total file
  size during forensic review.

These controls are justified because the ELF loader normally ignores appended
bytes: ordinary execution success alone cannot prove that an ELF contains no
hidden data. Authenticity checks, filesystem controls, and artifact inspection
address prevention, detection, and response as separate layers.

## Repository contents

- `src/main.c` — CLI and mode selection
- `src/format.c` — ELF validation and footer parsing
- `src/binder.c` — output bundle construction
- `src/runner.c` — embedded-program execution
- `src/io.c` — shared reliable I/O helpers
- `include/merge.h` — shared data structure and module declarations
- `tests/bin1.c`, `tests/bin2.c` — simple demonstration programs
- `tests/test_merge.sh` — usage, successful binding/execution, and invalid-input
  checks
- `Makefile` — build, test, and cleanup targets
- `subject.md`, `audit.md` — project specification and audit checklist

## Binary Analyst presentation notes

For an audit demonstration, show `make test`, inspect the files with
`readelf -h merge bin1 bin2 bin3`, and explain that `bin3` starts at the runner's
unchanged ELF entry point while the embedded programs keep their original ELF
images. Emphasize the fixed sequential process boundary, input validation,
atomic output creation, the dual-use risk of binders, and the layered defensive
recommendations above.
