# SGVM Specification

## 1. Overview
The Sage Virtual Machine (SGVM) is the portable execution substrate for SageOS. It provides a managed environment for system services and applications, ensuring isolation, safety, and architectural independence.

## 2. Compilation & Execution Pipeline
SageLang code follows a strictly defined path to execution:
1. **Source**: Human-readable `.sage` files.
2. **Compiler Frontend**: Parses source into an Abstract Syntax Tree (AST) or Intermediate Representation (SGIR).
3. **Bytecode Generation**: Emits portable SGVM instructions.
4. **Verification**: Mandatory security and safety checks.
5. **Runtime Execution**: Execution by the MetalVM engine.

## 3. Bytecode Verification
Before execution, SGVM bytecode MUST pass a verification pass that ensures:
- **Control Flow Integrity**: No illegal jumps or recursive depth violations.
- **Type Safety**: Operations are performed on valid operand types.
- **Boundary Checks**: No out-of-bounds access to memory or object arenas.
- **Capability Access**: The bytecode does not attempt to use restricted syscalls without proper permissions.

## 4. Execution Modes
SGVM supports multiple execution strategies:
- **Interpreted**: Direct execution of the AST or bytecode (default for kernel-mode scripts).
- **Threaded Interpreter**: High-performance bytecode dispatching using labels-as-values.
- **JIT/AOT (Future)**: Just-in-Time or Ahead-of-Time compilation to native machine code for performance-critical applications.

## 5. MetalVM C API (`metal_vm.h`)
The kernel interacts with SGVM via the following internal interfaces:
- `metal_vm_load_binary()`: Loads a compiled `.sgvm` artifact into memory.
- `metal_vm_run()` / `metal_vm_step()`: Executes bytecode instructions.
- `metal_vm_register_native()`: Binds kernel-level C functions to SageLang (the primary mechanism for exposing hardware to scripts).
- `sage_gil_acquire()` / `sage_gil_release()`: Serializes access to the VM state to maintain thread safety in SMP environments.

## 6. Object System & GC
SGVM features a reference-tracked object system with a built-in Mark-and-Sweep garbage collector. Objects (primarily Arrays and Dictionaries) are capability-tagged and reside in dedicated memory arenas.
