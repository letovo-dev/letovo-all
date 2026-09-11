#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 3 ]; then
  echo "usage: publish-bundle.sh BUNDLE_DIR EXPECTED_JSON TAG_MODE" >&2
  exit 2
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
bundle_dir="$(cd "$1" && pwd -P)"
expected_json="$(cd "$(dirname "$2")" && pwd -P)/$(basename "$2")"
tag_mode="$3"
manifest="$bundle_dir/manifest.json"
manifest_tool="$script_dir/image_manifest.py"

case "$tag_mode" in
  candidate|main|release) ;;
  *) echo "TAG_MODE must be candidate, main, or release" >&2; exit 2 ;;
esac

request_field() {
  python3 "$manifest_tool" field "$expected_json" "$1"
}

profile="$(request_field profile)"
job="$(request_field job)"
source_sha="$(request_field source_sha)"
if [ "$tag_mode" = candidate ]; then
  [ "$profile" = candidate ] && [ "$job" = pr ] || {
    echo "candidate tags require candidate PR metadata" >&2
    exit 2
  }
  candidate_number="$(request_field candidate_number)"
elif [ "$profile" != production ] || [ "$job" != "$tag_mode" ]; then
  echo "$tag_mode tags require matching production job metadata" >&2
  exit 2
fi

python3 "$manifest_tool" verify \
  "$manifest" "$expected_json" "$bundle_dir" --skip-image-inspect

for name in backend registration frontend uploader; do
  archive="$(python3 "$manifest_tool" image-field "$manifest" "$name" archive)"
  zstd -dc -- "$bundle_dir/$archive" | docker load
done

python3 "$manifest_tool" verify "$manifest" "$expected_json" "$bundle_dir"

digest_file="$bundle_dir/registry-digests.json"
digest_tmp="$bundle_dir/.registry-digests.json.tmp"
if [ -L "$digest_file" ] || [ -L "$digest_tmp" ]; then
  echo "registry digest output path is unsafe" >&2
  exit 2
fi
trap 'rm -f "$digest_tmp"' EXIT
printf '{"schema_version":1,"images":[' > "$digest_tmp"
first=true

record_tag() {
  local name="$1"
  local local_ref="$2"
  local target="$3"
  local digest
  docker tag "$local_ref" "$target"
  docker push "$target"
  digest="$(docker buildx imagetools inspect "$target" --format '{{json .Manifest.Digest}}')"
  digest="${digest#\"}"
  digest="${digest%\"}"
  if [[ ! "$digest" =~ ^sha256:[0-9a-f]{64}$ ]]; then
    echo "registry did not return a digest for $target" >&2
    exit 1
  fi
  if [ "$first" = true ]; then
    first=false
  else
    printf ',' >> "$digest_tmp"
  fi
  printf '{"name":"%s","reference":"%s","digest":"%s"}' \
    "$name" "$target" "$digest" >> "$digest_tmp"
}

for name in backend registration frontend uploader; do
  local_ref="$(python3 "$manifest_tool" image-field "$manifest" "$name" local_ref)"
  repository="$(python3 "$manifest_tool" image-field "$manifest" "$name" repository)"
  case "$tag_mode" in
    candidate)
      record_tag "$name" "$local_ref" "$repository:pr-${candidate_number}-${source_sha}"
      ;;
    main)
      record_tag "$name" "$local_ref" "$repository:$source_sha"
      record_tag "$name" "$local_ref" "$repository:latest"
      ;;
    release)
      record_tag "$name" "$local_ref" "$repository:$source_sha"
      ;;
  esac
done
printf ']}\n' >> "$digest_tmp"
mv "$digest_tmp" "$digest_file"
trap - EXIT
