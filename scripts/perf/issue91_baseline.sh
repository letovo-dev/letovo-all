#!/usr/bin/env bash
set -euo pipefail

BASE_URL="${BASE_URL:-https://letovocorp.ru}"
API_BASE="${API_BASE:-${BASE_URL}/letovo-api}"
OUT_DIR="${OUT_DIR:-/tmp/letovo-issue91-baseline}"
AUTH_TOKEN="${LETOVO_AUTH_TOKEN:-}"

mkdir -p "$OUT_DIR"

curl_public() {
  local name="$1"
  local url="$2"
  curl -sS -o "$OUT_DIR/${name}.body" \
    -w "${name} status=%{http_code} ttfb=%{time_starttransfer} total=%{time_total} bytes=%{size_download}\n" \
    "$url"
}

curl_api() {
  local name="$1"
  local url="$2"
  local auth_args=()
  if [ -n "$AUTH_TOKEN" ]; then
    auth_args=(-H "Authorization: Bearer ${AUTH_TOKEN}")
  fi
  curl -sS -o "$OUT_DIR/${name}.body" \
    -w "${name} status=%{http_code} ttfb=%{time_starttransfer} total=%{time_total} bytes=%{size_download}\n" \
    -H "Accept-Encoding: gzip" \
    "${auth_args[@]}" \
    "$url"
}

echo "Writing bodies to $OUT_DIR"
curl_public "home" "${BASE_URL}/"
curl_public "news_page" "${BASE_URL}/news"
curl_api "social_news" "${API_BASE}/social/news?start=0&size=10"
curl_api "social_titles" "${API_BASE}/social/titles"
curl_api "social_saved" "${API_BASE}/social/saved"
curl_api "chats" "${API_BASE}/chats/"

if [ -f "$OUT_DIR/chats.body" ]; then
  gzip -9c "$OUT_DIR/chats.body" | wc -c | awk '{print "chats gzip_estimate_bytes="$1}'
fi
if [ -f "$OUT_DIR/social_titles.body" ]; then
  gzip -9c "$OUT_DIR/social_titles.body" | wc -c | awk '{print "social_titles gzip_estimate_bytes="$1}'
fi
