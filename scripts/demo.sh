#!/usr/bin/env bash
set -euo pipefail

BASE_URL="${BASE_URL:-http://127.0.0.1:8080}"

create_account() {
  local owner="$1"
  curl --fail --silent --show-error \
    -X POST "$BASE_URL/v1/accounts" \
    -H 'Content-Type: application/json' \
    -d "{\"owner\":\"$owner\",\"currency\":\"USD\"}"
}

extract_id() {
  sed -n 's/.*"id":"\([^"]*\)".*/\1/p'
}

ALICE_JSON="$(create_account Alice)"
BOB_JSON="$(create_account Bob)"
ALICE_ID="$(printf '%s' "$ALICE_JSON" | extract_id)"
BOB_ID="$(printf '%s' "$BOB_JSON" | extract_id)"

printf 'Alice account: %s\n' "$ALICE_JSON"
printf 'Bob account:   %s\n' "$BOB_JSON"

curl --fail --silent --show-error \
  -X POST "$BASE_URL/v1/deposits" \
  -H 'Content-Type: application/json' \
  -d "{\"account_id\":\"$ALICE_ID\",\"amount_minor\":100000,\"idempotency_key\":\"demo-deposit-0001\"}"
printf '\n'

curl --fail --silent --show-error \
  -X POST "$BASE_URL/v1/transfers" \
  -H 'Content-Type: application/json' \
  -d "{\"source_account_id\":\"$ALICE_ID\",\"destination_account_id\":\"$BOB_ID\",\"amount_minor\":2500,\"idempotency_key\":\"demo-transfer-0001\"}"
printf '\n\nFinal balances:\n'
curl --fail --silent --show-error "$BASE_URL/v1/accounts/$ALICE_ID"
printf '\n'
curl --fail --silent --show-error "$BASE_URL/v1/accounts/$BOB_ID"
printf '\n'
