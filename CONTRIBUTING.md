# Contributing to AtlasPay

Thank you for your interest in contributing to AtlasPay.

AtlasPay is a modern C++20 financial services engine that demonstrates reliable backend engineering practices including transaction processing, double-entry accounting, idempotency, concurrency control, SQLite persistence, and REST API development.

---

# Development Environment

## Requirements

- C++20 compatible compiler (GCC 12+, Clang 15+)
- CMake 3.20+
- SQLite3
- Git
- cURL

### Ubuntu

```bash
sudo apt update
sudo apt install -y build-essential cmake libsqlite3-dev curl
```

### macOS

```bash
brew install cmake sqlite3 curl
```

---

# Clone

```bash
git clone git@github.com:Apple32452/atlaspay-cpp-financial-service.git
cd atlaspay-cpp-financial-service
```

---

# Build

```bash
cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Release

cmake --build build --parallel
```

---

# Run Tests

```bash
ctest --test-dir build --output-on-failure
```

All tests should pass before submitting a Pull Request.

---

# Run the Server

```bash
ATLASPAY_DB_PATH=./atlaspay.db \
ATLASPAY_BIND=127.0.0.1 \
ATLASPAY_PORT=8080 \
./build/atlaspay_server
```

Verify the service:

```bash
curl http://127.0.0.1:8080/healthz
```

Run the API demo:

```bash
chmod +x scripts/demo.sh
./scripts/demo.sh
```

---

# Development Workflow

Always create a new branch.

```bash
git switch main
git pull origin main

git switch -c feature/my-feature
```

Recommended branch names:

- feature/...
- fix/...
- docs/...
- test/...
- refactor/...

---

# Coding Guidelines

Please follow these principles.

- Use modern C++20.
- Keep functions small and readable.
- Prefer RAII.
- Avoid global state.
- Use descriptive variable names.
- Preserve transaction atomicity.
- Never violate double-entry accounting invariants.
- Use integer minor currency units.
- Write deterministic tests.
- Keep commits focused.

---

# Commit Messages

Good examples:

```
Add transaction rollback support

Improve SQLite error handling

Add concurrent transfer tests

Refactor HTTP request parser

Document API endpoints
```

Avoid messages such as

```
update

fix

changes

stuff
```

---

# Pull Requests

Each Pull Request should include

- Summary
- Motivation
- Validation

Example

```markdown
## Summary

- improve transfer validation
- add account freeze checks
- extend unit tests

## Motivation

Prevent transfers from frozen accounts.

## Validation

- Release build succeeds
- All CTest tests pass
```

---

# Checklist

Before opening a Pull Request

- [ ] Project builds successfully
- [ ] All tests pass
- [ ] Documentation updated if necessary
- [ ] No generated files committed
- [ ] No secrets committed
- [ ] Commits are focused
- [ ] Pull Request explains why the change was made

---

# Security

Never commit

- passwords
- API keys
- tokens
- certificates
- private keys
- customer information
- generated databases
- build artifacts

If you discover a security issue, please avoid publicly disclosing exploit details until the issue has been addressed.

---

# Code Style

- Follow existing project formatting.
- Keep interfaces stable when possible.
- Add tests for new functionality.
- Prefer readability over clever implementations.

---

# Questions

If you have questions about architecture, implementation, or design decisions, feel free to open an issue before beginning major work.

Thank you for helping improve AtlasPay.
