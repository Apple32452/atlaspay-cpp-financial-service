# Architecture Notes

## Consistency model

AtlasPay serializes balance-changing operations inside the service and executes each ledger mutation in a SQLite `BEGIN IMMEDIATE` transaction. A transfer inserts one transaction row and exactly two ledger entries: a debit to the source and an equal credit to the destination. The transaction is committed only after all writes succeed.

## Money representation

All amounts use signed 64-bit integer minor units. For USD, `2500` represents `$25.00`. Floating-point arithmetic is intentionally excluded from the payment path.

## Idempotency

Every deposit and transfer requires an idempotency key. The database enforces uniqueness. A replay with the same request fingerprint returns the original transaction, while reuse for a different payload returns a conflict.

## Risk controls

The default policy rejects:

- transfers above 1,000,000 minor units;
- outgoing volume above 2,500,000 minor units in 24 hours;
- more than five outgoing transfers in 60 seconds;
- frozen or closed accounts;
- cross-currency transfers.

## Production extensions

A production deployment should replace the demonstration HTTP parser with a hardened framework, terminate TLS at a trusted proxy, use authenticated principals and authorization policies, encrypt sensitive fields, stream audit events to immutable storage, and run migrations under explicit version control.
