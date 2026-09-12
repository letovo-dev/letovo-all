#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 3 ]; then
  echo "usage: build-builder.sh SOURCE_ROOT REQUEST_JSON OUTPUT_DIR" >&2
  exit 2
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
source_root="$(cd "$1" && pwd -P)"
request_json="$(cd "$(dirname "$2")" && pwd -P)/$(basename "$2")"
output_dir="$3"
tool="$script_dir/builder_artifact.py"
identity_tool="$script_dir/image_manifest.py"

python3 "$tool" validate-request "$request_json"
actual_revisions="$(python3 "$tool" revisions "$source_root")"
python3 - "$request_json" "$actual_revisions" <<'PY'
import json, sys
request = json.load(open(sys.argv[1]))
actual = json.loads(sys.argv[2])
if any(request[name] != value for name, value in actual.items()):
    raise SystemExit("builder source revisions differ from request")
PY
if [ -L "$output_dir" ] || [ -e "$output_dir" ]; then
  echo "builder output must not exist" >&2
  exit 2
fi
mkdir -p "$output_dir/images" "$output_dir/reports"

field() { python3 "$tool" field "$request_json" "$1"; }
run_id="$(field run_id)"
run_attempt="$(field run_attempt)"
revision="$(field builder_revision)"
base_image="$(sed -n 's/^BASE_IMAGE=//p' "$source_root/src/backend-builder.env")"
[[ "$base_image" =~ ^ubuntu@sha256:[0-9a-f]{64}$ ]] || {
  echo "invalid or duplicate BASE_IMAGE" >&2
  exit 2
}
reference="letovo-ci/backend-builder:${run_id}-${run_attempt}-${revision}"

docker buildx build --platform linux/amd64 --load \
  --file "$source_root/src/Dockerfile.builder" \
  --build-arg "BASE_IMAGE=$base_image" --tag "$reference" "$source_root/src"

docker run --rm --platform linux/amd64 --entrypoint sh "$reference" -ec '
  test -f /opt/letovo/cmake/jwt-cpp-config.cmake
  test -f /opt/letovo/lib/cmake/llhttp/llhttp-config.cmake
  test -f /opt/letovo/lib/cmake/opentelemetry-cpp/opentelemetry-cpp-config.cmake
  test -f /opt/letovo/share/cmake/nlohmann_json/nlohmann_jsonConfig.cmake
  test -f /usr/include/boost/format.hpp
  command -v ninja
'

inspection="$(docker image inspect --format '{{.Id}} {{.Architecture}} {{.Os}}' "$reference")"
[[ "$inspection" =~ ^sha256:[0-9a-f]{64}\ amd64\ linux$ ]] || {
  echo "unexpected backend builder identity: $inspection" >&2
  exit 1
}
printf '%s\n' '{"schema_version":1,"status":"success","inspections":["/opt/letovo/cmake/jwt-cpp-config.cmake","/opt/letovo/lib/cmake/llhttp/llhttp-config.cmake","/opt/letovo/lib/cmake/opentelemetry-cpp/opentelemetry-cpp-config.cmake","/opt/letovo/share/cmake/nlohmann_json/nlohmann_jsonConfig.cmake","/usr/include/boost/format.hpp","ninja"]}' \
  > "$output_dir/reports/backend-builder.json"
raw_image="$output_dir/images/.backend-builder.tar"
trap 'rm -f "$raw_image"' EXIT
docker save "$reference" > "$raw_image"
identity="$(python3 "$identity_tool" saved-image-id "$raw_image" "$reference")"
[[ "$identity" =~ ^(sha256:[0-9a-f]{64})\ amd64\ linux$ ]] || exit 2
image_id="${BASH_REMATCH[1]}"
zstd -1 -T0 -o "$output_dir/images/backend-builder.tar.zst" < "$raw_image"
rm -f "$raw_image"
trap - EXIT
python3 "$tool" create-manifest "$request_json" "$output_dir" "$reference" "$image_id"
python3 "$tool" verify-result "$request_json" "$output_dir"
