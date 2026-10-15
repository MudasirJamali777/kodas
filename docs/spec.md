# Kodas Language Specification (v0.1 Draft)

## 1. Overview

Kodas is a statically-typed (in later phases), general-purpose programming language designed for readability, safety, and performance.

### File extension
`.kds`

### Comments
```kodas
-- single line comment
---
  multi-line
  comment
---
```

## 2. Variables and Bindings

### `let` - Mutable by default in seed, immutable with explicit syntax
```kodas
let x = 10           -- immutable in v1.0 plan; mutable in seed
let mut counter = 0  -- explicitly mutable
```

### `seal` - Constant (immutable)
```kodas
seal PI = 3.14159
seal GREETING = "Hello"
```

### Type annotations
```kodas
let age: Int = 25
let name: String = "Alice"
```

## 3. Primitive Types

| Type | Description | Example |
|------|-------------|---------|
| `Int` | 64-bit signed integer | `42` |
| `Float` | 64-bit float | `3.14` |
| `Bool` | Boolean | `true`, `false` |
| `String` | UTF-8 string | `"hello"` |
| `Char` | Single character | `'a'` |
| `Byte` | 8-bit byte | `0xFF` |

## 4. Collections

### List
```kodas
let numbers = [1, 2, 3, 4, 5]
let first = numbers[0]
numbers.push(6)
```

### Map
```kodas
let m = {"name": "Alice", "age": 30}
```

### Tuple
```kodas
let p = (42, "hi")
let x = p.0
```

### Range
```kodas
0..10    -- exclusive: 0,1,...,9
0..=10   -- inclusive: 0,1,...,10
```

## 5. Functions

### Declaration
```kodas
fn add(a: Int, b: Int) -> Int {
    give a + b
}
```

### Arrow function
```kodas
fn double(x: Int) -> Int => x * 2
```

### Anonymous function (lambda)
```kodas
let sq = fn(x) => x * 2
numbers.map(fn(n) => n * 2)
```

### Return values
Use `give` (not `return`):
```kodas
fn abs(x) {
    if x < 0 { give -x }
    give x
}
```

### Default parameters
```kodas
fn greet(name = "World") => "Hello, {name}!"
```

## 6. Control Flow

### If/else
```kodas
if age >= 18 {
    show "Adult"
} else if age >= 13 {
    show "Teen"
} else {
    show "Child"
}
```

### Ternary expression
```kodas
let status = if age >= 18 then "adult" else "minor"
```

### Match
```kodas
match value {
    1     => show "one"
    2..10 => show "small"
    _     => show "other"
}
```

### Loops
```kodas
loop 10 { show "repeat" }
loop item in collection { show item }
loop i, item in collection { show "{i}: {item}" }
loop while condition { ... }
loop { if done then stop }
```

Use `stop` (break) and `skip` (continue).

## 7. Structs

```kodas
struct User {
    name: String
    age: Int
    email: String
    verified: Bool = false
}
```

## 8. Error Handling

```kodas
fn safe_div(a, b) {
    if b == 0 { give err("division by zero") }
    give ok(a / b)
}

match safe_div(10, 0) {
    ok(v) => show v
    err(e) => show "Error: {e}"
}
```

## 9. String Interpolation

```kodas
show "Hello, {name}! Age: {age}"
```

## 10. Built-in Tests

```kodas
test "addition works" {
    assert 2 + 2 == 4
}
```

## 11. Operators

| Precedence | Operators |
|------------|-----------|
| 1 (highest) | `.` `()` `[]` |
| 2 | unary `-` `!` |
| 3 | `*` `/` `%` |
| 4 | `+` `-` |
| 5 | `..` `..=` |
| 6 | `<` `>` `<=` `>=` |
| 7 | `==` `!=` |
| 8 | `&&` |
| 9 | `\|\|` |
| 10 | `|>` `\|?>` |
| 11 (lowest) | `=` `+=` `-=` `*=` `/=` |

## 12. Keywords (reserved)

`let`, `seal`, `show`, `fn`, `if`, `else`, `match`, `loop`, `while`, `in`,
`stop`, `skip`, `give`, `struct`, `enum`, `behave`, `trait`, `is`, `pub`,
`as`, `module`, `import`, `use`, `mut`, `true`, `false`, `maybe`, `empty`,
`ok`, `err`, `Result`, `never`, `any`, `flow`, `together`, `timeout`, `Channel`,
`bridge`, `test`, `bench`, `assert`, `measure`, `contract`, `then`, `otherwise`,
`demand`, `for`, `op`, `self`, `new`, `type`, `where`
