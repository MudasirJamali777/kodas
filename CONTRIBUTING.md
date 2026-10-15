# Contributing to Kodas

Thank you for your interest in Kodas! We welcome contributions.

## Getting Started

1. Fork the repository
2. Clone your fork: `git clone https://github.com/MudasirJamali777/kodas.git`
3. Build the seed compiler: `cd seed && make`
4. Run the tests: `make test`

## Development Workflow

- Create a feature branch: `git checkout -b feat/my-feature`
- Make changes, following the code style
- Add tests for new functionality
- Ensure all tests pass
- Commit using conventional commits (see below)
- Push and open a pull request

## Conventional Commits

We use [Conventional Commits](https://www.conventionalcommits.org/):

- `feat(lexer): add float literal support`
- `fix(parser): correct precedence of unary ops`
- `test(stdlib): add test cases for List`
- `docs(spec): document trait system`
- `refactor(eval): extract pattern matching`
- `perf(gc): reduce allocation overhead`

## Code Style

- C code: 4-space indent, `snake_case` for functions, `UPPER_SNAKE` for macros
- Kodas code: 4-space indent; formatter (`kodas fmt`) will enforce
- Maximum function length: 50 lines
- All public functions should have `///` doc comments
- Zero compiler warnings
- All tests pass

## Testing

After implementing any feature:
1. Write test cases in `tests/`
2. Run `make test` from `seed/`
3. Fix failures before opening a PR

## Areas Where Help is Welcome

- Native code generation (LLVM, WASM)
- Standard library modules (web, db, crypto)
- IDE (KodasForge) - Tauri + Svelte/React
- LSP server implementation
- Concurrency runtime (threads, channels, thread pool)
- Package manager (Kargo)
- Documentation & tutorials

## License

By contributing, you agree that your contributions will be licensed under the GPL-3.0 license.

## Code of Conduct

- Be respectful and inclusive
- Focus on what's best for the community
- Welcome newcomers

Happy hacking! 🚀
