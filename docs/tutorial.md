# Kodas Tutorial: Zero to Hero

Welcome to Kodas! This tutorial will take you from "Hello World" to writing real programs.

## 1. Hello, World

Create a file `hello.kds`:

```kodas
show "Hello, World!"
```

Run it:
```bash
kodas run hello.kds
```

## 2. Variables

```kodas
let name = "Alice"
let mut age = 30
age = age + 1
seal PI = 3.14159

show "{name} is {age} years old"
```

- `let` creates a binding (immutable by convention, but mutable in seed)
- `let mut` creates an explicitly mutable variable
- `seal` creates a constant that cannot be changed

## 3. Functions

```kodas
fn greet(name) => "Hi, {name}!"

fn factorial(n) {
    if n <= 1 { give 1 }
    give n * factorial(n - 1)
}

show greet("World")
show factorial(10)
```

- `fn` declares a function
- `=>` is used for single-expression (arrow) functions
- `give` returns a value (instead of `return`)

## 4. Conditionals

```kodas
let x = 15
if x >= 18 {
    show "Adult"
} else {
    show "Minor"
}

let status = if x >= 18 then "adult" else "minor"
```

## 5. Lists

```kodas
let numbers = [1, 2, 3, 4, 5]
show numbers.len
numbers.push(6)
show numbers

let doubled = numbers.map(fn(n) => n * 2)
let evens = numbers.filter(fn(n) => n % 2 == 0)
let total = numbers.reduce(fn(a, b) => a + b, 0)
```

## 6. Loops

```kodas
loop i in 1..10 {
    show i
}

loop 3 { show "repeat!" }

let mut n = 0
loop while n < 5 {
    n = n + 1
}
```

## 7. Pattern Matching

```kodas
match value {
    0 => "zero"
    1 => "one"
    _ => "something else"
}
```

## 8. Structs

```kodas
struct Point { x: Float, y: Float }
let p = Point { x: 1.0, y: 2.0 }
show "({p.x}, {p.y})"
```

## 9. Error Handling

```kodas
fn safe_div(a, b) {
    if b == 0 { give err("division by zero") }
    give ok(a / b)
}
```

## 10. Tests

```kodas
test "addition works" {
    assert 2 + 2 == 4
}
```

Run tests with:
```bash
kodas test myfile.kds
```

## Next Steps

- Read `docs/spec.md` for the full language specification
- Check out the `examples/` directory for sample programs
- Explore the standard library in `stdlib/`
