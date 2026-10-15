# Kodas Compiler Architecture

## Overview

The Kodas compiler is organized in phases that progressively transform source code into executable output.

## Seed Compiler (C)

### Pipeline
```
Source Code → Lexer → Tokens → Parser → AST → Evaluator → Values
```

### 1. Lexer (`lexer.h`, `lexer.c`)
Converts raw source text into a stream of tokens. Handles:
- Identifiers and keywords
- Numeric literals (integer, float, scientific notation)
- String literals (regular, raw, multiline, interpolated)
- Character literals
- Operators (including multi-character: `->`, `=>`, `|>`, `|?>`, `..`, `..=`)
- Comments (line `--`, block `--- ... ---`)

### 2. Parser (`parser.h`, `parser.c`)
Recursive descent parser with Pratt parsing for expressions. Produces an AST.
- Precedence climbing for binary operators
- Statement and expression parsing
- Pattern parsing for `match`
- Structural parsing for `fn`, `struct`, `enum`, `behave`, `trait`

### 3. AST (`ast.h`, `ast.c`)
Tree representation of the program. Uses tagged unions for expression variants.

### 4. Values (`value.h`, `value.c`)
Runtime value representation: tagged union covering Int, Float, Bool, String, List, Map, Tuple, Closure, NativeFn, StructInstance, EnumVariant, Result.

### 5. Environment (`environment.h`, `environment.c`)
Lexical scoping with chained hash tables. Holds variable bindings and built-in functions.

### 6. Evaluator (`eval.h`, `eval.c`)
Tree-walking interpreter. Evaluates expressions and statements recursively. Handles closures via captured environments.

### 7. Main (`main.c`)
CLI entry point with subcommands: run, build, check, repl, tokens, ast.

## Self-Hosting Plan

Once the seed compiler supports enough of the language, we will:
1. Rewrite the compiler in Kodas itself under `self/`
2. Compile it with the seed compiler → `kodas2`
3. Compile it with `kodas2` → `kodas3`
4. Verify `kodas2` and `kodas3` produce identical output (stage3 verification)

## Code Generation (Phase 2)

Backends planned:
- **C backend**: Transpiles to C for easy bootstrapping and portability
- **LLVM backend**: Full optimization and native performance
- **WASM backend**: Browser execution
- **JS backend**: Easy interop with JavaScript ecosystem

## Memory Management

Seed interpreter relies on OS cleanup on exit (short-lived programs). Self-hosted version will use reference counting with cycle detection or a mark-and-sweep GC.
