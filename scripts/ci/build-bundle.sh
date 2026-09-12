#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 3 ]; then
  echo "usage: build-bundle.sh SOURCE_ROOT REQUEST_JSON OUTPUT_DIR" >&2
  exit 2
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
source_root="$(cd "$1" && pwd -P)"
request_json="$(cd "$(dirname "$2")" && pwd -P)/$(basename "$2")"
output_dir="$3"
manifest_tool="$script_dir/image_manifest.py"

python3 "$manifest_tool" validate-request "$request_json"
if [ -L "$output_dir" ] || { [ -e "$output_dir" ] && [ ! -d "$output_dir" ]; }; then
  echo "output directory is unsafe" >&2
  exit 2
fi
if [ -d "$output_dir" ] && [ -n "$(find "$output_dir" -mindepth 1 -print -quit)" ]; then
  echo "output directory is not empty" >&2
  exit 2
fi
mkdir -p "$output_dir/images" "$output_dir/reports"

for name in ServerConfig SqlConnectionConfig PagesConfig MarketConfig; do
  link="$source_root/src/configs/${name}.json"
  target="/mnt/server-configs/${name}.json"
  if [ -L "$link" ]; then
    [ "$(readlink "$link")" = "$target" ] || {
      echo "runtime config symlink has wrong target: $link" >&2
      exit 2
    }
  elif [ -e "$link" ]; then
    echo "runtime config path must be the expected symlink or absent: $link" >&2
    exit 2
  else
    ln -s "$target" "$link"
  fi
done

request_field() {
  python3 "$manifest_tool" field "$request_json" "$1"
}

run_id="$(request_field run_id)"
run_attempt="$(request_field run_attempt)"
profile="$(request_field profile)"
source_sha="$(request_field source_sha)"
base_url="$(request_field base_url)"
verification_base_url="https://letovocorp.ru"
expected_builder="$(request_field builder_image)"
build_files="$(request_field build_files)"
local_suffix="${run_id}-${run_attempt}-${profile}-${source_sha}"
env_file="$(mktemp)"
summary_file="$(mktemp)"
postgres_name=""
raw_image=""

cleanup() {
  if [ -n "$postgres_name" ]; then
    docker rm -f "$postgres_name" >/dev/null 2>&1 || true
  fi
  rm -f "$env_file" "$summary_file"
  [ -z "$raw_image" ] || rm -f "$raw_image"
}
trap cleanup EXIT

(
  cd "$source_root"
  GITHUB_ENV="$env_file" GITHUB_STEP_SUMMARY="$summary_file" \
    bash scripts/export_backend_builder.sh
)
mapfile -t resolved_builders < <(sed -n 's/^BUILDER_IMAGE=//p' "$env_file")
if [ "${#resolved_builders[@]}" -ne 1 ] || [ "${resolved_builders[0]}" != "$expected_builder" ]; then
  echo "request builder_image does not match scripts/export_backend_builder.sh" >&2
  exit 2
fi
builder_image="${resolved_builders[0]}"
if [ -n "${LOCAL_BUILDER_REF:-}${LOCAL_BUILDER_IMAGE_ID:-}" ]; then
  [ "${LOCAL_BUILDER_REF:-}" = "letovo-ci/backend-builder:${run_id}-${run_attempt}" ] || exit 2
  [[ "${LOCAL_BUILDER_IMAGE_ID:-}" =~ ^sha256:[0-9a-f]{64}$ ]] || exit 2
  [ "$(docker image inspect --format '{{.Id}} {{.Architecture}} {{.Os}}' "$LOCAL_BUILDER_REF")" = "$LOCAL_BUILDER_IMAGE_ID amd64 linux" ] || exit 2
  # docker save/load loses RepoDigests; this verified local alias avoids a registry lookup.
  builder_image="$LOCAL_BUILDER_REF"
fi

docker run --rm --platform linux/amd64 \
  --volume "$source_root:/work:ro" --workdir /work "$builder_image" \
  bash -lc 'g++ -std=c++20 -fsanitize=address,undefined -fno-omit-frame-pointer test/avatar_policy_regression.cc -o /tmp/avatar-policy-regression && /tmp/avatar-policy-regression'

postgres_name="letovo-ci-pg-${run_id}-${run_attempt}-$$"
postgres_database="letovo_ci_${run_id}_${run_attempt}"
docker run --detach --rm --platform linux/amd64 --name "$postgres_name" \
  --env POSTGRES_USER=postgres --env POSTGRES_HOST_AUTH_METHOD=trust \
  --env "POSTGRES_DB=$postgres_database" --publish 127.0.0.1::5432 postgres:16 >/dev/null
for _ in {1..60}; do
  if docker exec "$postgres_name" pg_isready -U postgres -d "$postgres_database" >/dev/null 2>&1; then
    break
  fi
  sleep 1
done
docker exec "$postgres_name" pg_isready -U postgres -d "$postgres_database" >/dev/null
postgres_port="$(docker port "$postgres_name" 5432/tcp | head -n 1)"
postgres_port="${postgres_port##*:}"
postgres_dsn="postgresql://postgres@127.0.0.1:${postgres_port}/${postgres_database}?connect_timeout=1"
for _ in {1..60}; do
  if LETOVO_POSTGRES_DSN="$postgres_dsn" python3 -c \
    'import os, psycopg2; psycopg2.connect(os.environ["LETOVO_POSTGRES_DSN"]).close()' \
    2>/dev/null; then
    break
  fi
  sleep 1
done
LETOVO_POSTGRES_DSN="$postgres_dsn" python3 -c \
  'import os, psycopg2; psycopg2.connect(os.environ["LETOVO_POSTGRES_DSN"]).close()'
LETOVO_POSTGRES_DSN="$postgres_dsn" \
  python3 -m pytest -q \
    "$source_root/test/test_issue193_department_payout_postgres.py" \
    "$source_root/test/test_issue179_media_order_postgres.py"
docker rm -f "$postgres_name" >/dev/null
postgres_name=""

(
  cd "$source_root/frontend"
  npm ci
  npm run test:opentelemetry
  npm run test:post-media-order
  npm run test:image-lightbox
  npm run test:article-editor-modes
  NEXT_TELEMETRY_DISABLED=1 \
    NEXT_PUBLIC_BASE_URL="$verification_base_url/letovo-api" \
    NEXT_PUBLIC_BASE_URL_UPLOAD="$verification_base_url/letovo-api/upload/" \
    NEXT_PUBLIC_BASE_URL_MEDIA="$verification_base_url/letovo-api/media/get" \
    NEXT_PUBLIC_UPLOAD_URL="$verification_base_url/letovo-api/upload/" \
    NEXT_PUBLIC_BASE_URL_CLEAR="$verification_base_url" \
    LETOVO_BUILD_SHA="$source_sha" \
    NEXT_PUBLIC_OTEL_ENABLED=true \
    NEXT_PUBLIC_OTEL_EXPORTER_OTLP_TRACES_ENDPOINT=/otel/v1/traces \
    NEXT_PUBLIC_OTEL_SERVICE_NAME=letovo-frontend \
    NEXT_PUBLIC_OTEL_DEPLOYMENT_ENVIRONMENT=production \
    NEXT_PUBLIC_OTEL_SERVICE_NAMESPACE=letovocorp \
    NEXT_PUBLIC_OTEL_TRACES_SAMPLER_RATIO=0.1 \
    NEXT_PUBLIC_LETOVO_BUILD_SHA="$source_sha" \
    npm run build
  [ -d .next ] || { echo "Frontend build output .next is missing" >&2; exit 1; }
  if grep -R -I -n -E 'ya\.sergeiscv\.ru|/undefined/auth|/letovo-api/letovo-api' .next; then
    scan_status=0
  else
    scan_status=$?
  fi
  case "$scan_status" in
    0) echo "Found forbidden frontend route or host in built bundle" >&2; exit 1 ;;
    1) ;;
    *) echo "Frontend route scan failed with status $scan_status" >&2; exit "$scan_status" ;;
  esac
)

backend_ref="letovo-ci/backend:$local_suffix"
registration_ref="letovo-ci/registration:$local_suffix"
frontend_ref="letovo-ci/frontend:$local_suffix"
uploader_ref="letovo-ci/uploader:$local_suffix"

docker buildx build --platform linux/amd64 --load \
  --file "$source_root/src/Dockerfile" \
  --build-arg "BUILDER_IMAGE=$builder_image" \
  --build-arg MAIN_FILE=server.cpp \
  --build-arg TEST_FILE=test.cpp \
  --build-arg "BUILD_FILES=$build_files" \
  --build-arg "LETOVO_BUILD_SHA=$source_sha" \
  --tag "$backend_ref" "$source_root/src"

docker buildx build --platform linux/amd64 --load \
  --file "$source_root/src/Dockerfile" \
  --build-arg "BUILDER_IMAGE=$builder_image" \
  --build-arg MAIN_FILE=registration_server.cpp \
  --build-arg TEST_FILE=test.cpp \
  --build-arg "BUILD_FILES=$build_files" \
  --build-arg "LETOVO_BUILD_SHA=$source_sha" \
  --tag "$registration_ref" "$source_root/src"

docker buildx build --platform linux/amd64 --load \
  --file "$source_root/frontend/dockerfile" \
  --build-arg "LETOVO_BUILD_SHA=$source_sha" \
  --build-arg "NEXT_PUBLIC_BASE_URL=$base_url/letovo-api" \
  --build-arg "NEXT_PUBLIC_BASE_URL_UPLOAD=$base_url/letovo-api/upload/" \
  --build-arg "NEXT_PUBLIC_BASE_URL_MEDIA=$base_url/letovo-api/media/get" \
  --build-arg "NEXT_PUBLIC_UPLOAD_URL=$base_url/letovo-api/upload/" \
  --build-arg "NEXT_PUBLIC_BASE_URL_CLEAR=$base_url" \
  --build-arg NEXT_PUBLIC_OTEL_ENABLED=true \
  --build-arg NEXT_PUBLIC_OTEL_EXPORTER_OTLP_TRACES_ENDPOINT=/otel/v1/traces \
  --build-arg NEXT_PUBLIC_OTEL_SERVICE_NAME=letovo-frontend \
  --build-arg NEXT_PUBLIC_OTEL_DEPLOYMENT_ENVIRONMENT=production \
  --build-arg NEXT_PUBLIC_OTEL_SERVICE_NAMESPACE=letovocorp \
  --build-arg NEXT_PUBLIC_OTEL_TRACES_SAMPLER_RATIO=0.1 \
  --build-arg "NEXT_PUBLIC_LETOVO_BUILD_SHA=$source_sha" \
  --tag "$frontend_ref" "$source_root/frontend"

docker buildx build --platform linux/amd64 --load \
  --file "$source_root/src/python-helpers/dockerfile.uploader" \
  --build-arg "UPLOADER_CAPABILITIES_URL=$base_url/letovo-api/auth/amiuploader" \
  --tag "$uploader_ref" "$source_root/src/python-helpers"

image_ids=()
for name in backend registration frontend uploader; do
  reference="letovo-ci/${name}:$local_suffix"
  inspection="$(docker image inspect --format '{{.Id}} {{.Architecture}} {{.Os}}' "$reference")"
  if [[ ! "$inspection" =~ ^sha256:[0-9a-f]{64}\ amd64\ linux$ ]]; then
    echo "unexpected image identity for $name: $inspection" >&2
    exit 1
  fi
  printf '{"status":"success"}\n' > "$output_dir/reports/$name.json"
  raw_image="$output_dir/images/.$name.tar"
  docker save "$reference" > "$raw_image"
  identity="$(python3 "$manifest_tool" saved-image-id "$raw_image" "$reference")"
  [[ "$identity" =~ ^(sha256:[0-9a-f]{64})\ amd64\ linux$ ]] || exit 2
  image_ids+=("${BASH_REMATCH[1]}")
  zstd -1 -T0 -o "$output_dir/images/$name.tar.zst" < "$raw_image"
  rm -f "$raw_image"
  raw_image=""
done

python3 "$manifest_tool" create "$request_json" "$output_dir" "${image_ids[@]}"
