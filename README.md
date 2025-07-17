# llvm-cfg - Embed Global CFG in Executable File

> Coming soon.
> Not finished yet. Hang on~

# Prerequisites

## LLVM

This project is known to work with llvm-18.1.0 and llvm-15.0.7,
with the following configuration:
```sh
cmake .. -DCMAKE_BUILD_TYPE=Debug \
    -DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra;lld;polly" \
    -DLLVM_TARGETS_TO_BUILD=Native \
    -DLLVM_ENABLE_RUNTIMES="compiler-rt"
```

# How it works

## Instrumentation
Instrument a program with sanitizer coverage 
 `-fsanitize-coverage=trace-pc-guard,pc-table,no-prune`.

## Build Intra-Function Control Flow 

<ul>
    <li> Address a basic block by its argument to __sanitizer_cov_trace_pc_guard.</li>
    <li> Record each edge (src, dst) in a separate section called __sancov_cfg_edges. </li>
</ul>

> This is done with [CfgEdgePass.cpp](./pass/cfg-edge/CfgEdgePass.cpp).

