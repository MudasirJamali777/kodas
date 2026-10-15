# Kodas Cookbook

Common patterns and recipes for writing Kodas.

## Table of Contents
- [Reading a file](#reading-a-file)
- [HTTP requests](#http-requests)
- [Working with lists](#working-with-lists)
- [Error handling](#error-handling)
- [Web server](#web-server)
- [CLI arguments](#cli-arguments)

## Reading a file

```kodas
let result = read_file("config.json")
match result {
    ok(content) => show content
    err(e) => show "Failed to read: {e}"
}
```

## Working with lists

```kodas
let numbers = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]

let evens = numbers.filter(fn(n) => n % 2 == 0)
let doubled = evens.map(fn(n) => n * 2)
let total = doubled.reduce(fn(a, b) => a + b, 0)
show total  -- 60
```

## Error handling with |?>

```kodas
let name = read_file("data.json")
    |?> parse_json
    |?> validate
    |?> fn(d) => d.name
    otherwise "unknown"
```

## Simple web server (planned)

```kodas
import web

web.app {
    get "/" fn(req) => html "<h1>Hello!</h1>"
    get "/api/greet/:name" fn(req) => json({hello: req.params.name})
    listen 3000
}
```

## FizzBuzz

```kodas
loop i in 1..101 {
    if i % 15 == 0 { show "FizzBuzz" }
    else if i % 3 == 0 { show "Fizz" }
    else if i % 5 == 0 { show "Buzz" }
    else { show i }
}
```

## Factorial

```kodas
fn factorial(n) {
    if n <= 1 { give 1 }
    give n * factorial(n - 1)
}
```

## Fibonacci

```kodas
fn fib(n) {
    if n <= 1 { give n }
    give fib(n - 1) + fib(n - 2)
}
```
