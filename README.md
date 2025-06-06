# Q++

**Q++** is a modern programming language built to extend C++ into the quantum era.  
Inspired by how C++ evolved from C, Q++ introduces just enough new logic to support quantum-classical hybrid computing — while keeping everything great about systems-level development.

---

## 📌 Main Goals

- Build Q++ using C and C++ the way C++ was built from C
- Document how to migrate from C++ to Q++ with minimal cognitive overhead
- Let quantum logic flow naturally through familiar constructs like `struct`, `register`, `bool`, and `function`

---

## 🔧 What Q++ Adds

| Feature | Q++ Evolution |
|--------|----------------|
| `struct`, `class` | `qstruct`, `qclass`, `cstruct`, `cclass` for hybrid modeling |
| `bool` | Automatically probabilistic if linked to quantum memory |
| Bitwise Ops | Become quantum gate macros (`^` → `CX`, `&` → `Toffoli`) |
| `register` | Extended as `qregister`, `cregister`, or left as `auto` |
| `task<T>` | `task<QPU>`, `task<CPU>`, `task<AUTO>` target hardware like GPU kernels |
| `__qasm` | Inject raw gate-level code (like inline asm) |
| LLVM IR | Enhanced with QIR, collapse metadata, and probabilistic flags |

---

## 📘 Two Main Project Areas

### 📚 1. Developer Docs
Found in `/docs`. Help us:
- Translate C++ constructs into Q++ logic
- Define rules for probabilistic branching and hybrid task types
- Help classical devs think in quantum terms

### 🛠️ 2. Compiler Development
Found in `/src`. Help us:
- Parse new `qstruct`, `task<CPU>`, and `qregister` keywords
- Build IR with gate macros, entanglement tracking, and auto-collapse
- Simulate CPU/QPU behavior in a fallback runtime

---

## ✨ Philosophy

> No coders' pride just scribes:
> 
> It's C++ but with some patches under the hood to describe quantum bits and their logic where necessary.
> This provides a shared perspective for coding on computers with CPUs and QPUs attached to them.
>
> Q++ assumes all bits are quantum (qbits) or classical (cbits) information unless specified.  
> It treats classical logic as a special case of general information.

---

## 🤝 Want to Contribute?

Open an issue or pick a task from the [docs folder](https://github.com/sefunmi4/qpp-lang/blob/main/docs/overview.md#q-docs-overview).

### 🧪 Wavefunction Prime Demo

We now include `tools/wave_primes.py`, a short Python script that explores
finding primes via interference patterns. Use it to plot the composite wave for
the first few primes:

```bash
python tools/wave_primes.py --plot --method square -n 50
```

The demo is purely experimental but serves as a playground for ideas inspired by
quantum Fourier transforms.


## Building and Testing

The project uses CMake. A typical build workflow is:

```bash
mkdir build && cd build
cmake ..
make
ctest
```

`ctest` executes the small wavefunction simulator tests.

### Python Requirements

To run the demo in `tools/wave_primes.py` install dependencies via:

```bash
pip install -r requirements.txt
```

For contribution guidelines see [CONTRIBUTING.md](CONTRIBUTING.md).

### Quick Start Example

Compile and run the sample program in `docs/examples/demo.qpp` with:

```bash
qppc docs/examples/demo.qpp demo.ir
qpp-run demo.ir
```

This demonstrates the toy toolchain using the runtime scheduler and wavefunction simulator.

### Open Tasks

See [TODO.md](TODO.md) for current areas where help is welcome. Items marked
`good-first-issue` are ideal starting points for new contributors. The file also
contains a **Longer Term Roadmap** describing features like a full compiler,
advanced scheduler, and hardware API integration that would move Q++ toward
production readiness.

### Editor Support

For syntax highlighting in VS Code, a minimal extension is provided under
`/vscode`. Launch VS Code with the extension enabled using:

```bash
code --extensionDevelopmentPath=vscode .
```

This registers the `.qpp` extension with basic grammar definitions so Q++
files render cleanly without C++ template errors.

