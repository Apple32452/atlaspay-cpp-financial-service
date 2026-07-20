# Publish AtlasPay to GitHub

This source tree is already initialized as a Git repository with commits.

## 1. Create the empty repository

In the GitHub account `Apple32452`, create a new empty repository named:

```text
atlaspay-cpp-financial-service
```

Do not add a generated README, `.gitignore`, or license because those files are already included.

## 2. Push this project

From the project directory:

```bash
git branch -M main
git remote add origin https://github.com/Apple32452/atlaspay-cpp-financial-service.git
git push -u origin main
```

If `origin` already exists:

```bash
git remote set-url origin https://github.com/Apple32452/atlaspay-cpp-financial-service.git
git push -u origin main
```

## 3. Recommended repository description

```text
Linux/C++20 financial services engine with a double-entry ledger, atomic SQLite transfers, idempotent APIs, fraud controls, POSIX HTTP serving, systemd, Docker, tests, and CI.
```

## 4. Recommended GitHub topics

```text
cpp linux fintech payment-system double-entry-ledger sqlite cmake systemd docker idempotency fraud-detection
```
