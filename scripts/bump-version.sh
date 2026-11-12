#!/usr/bin/env bash
# bump-version.sh — increment the patch version and update all version references
#
# Usage:
#   ./scripts/bump-version.sh           # auto-increment patch  (1.0.6 → 1.0.7)
#   ./scripts/bump-version.sh minor     # increment minor, reset patch (1.0.6 → 1.1.0)
#   ./scripts/bump-version.sh major     # increment major, reset minor+patch (1.0.6 → 2.0.0)
#   ./scripts/bump-version.sh 2.1.0     # set an explicit version
#
# Files updated:
#   VERSION                       — single source of truth
#   android/app/build.gradle      — versionCode + versionName
#   web/index.html                — APK href/download/label defaults
#   web/commit.json               — appVersion for local dev serving

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION_FILE="$REPO_ROOT/VERSION"
GRADLE_FILE="$REPO_ROOT/android/app/build.gradle"
HTML_FILE="$REPO_ROOT/web/index.html"
COMMIT_JSON="$REPO_ROOT/web/commit.json"

if [[ ! -f "$VERSION_FILE" ]]; then
  echo "ERROR: $VERSION_FILE not found." >&2
  exit 1
fi

current=$(tr -d '[:space:]' < "$VERSION_FILE")
IFS='.' read -r major minor patch <<< "$current"

bump="${1:-patch}"

case "$bump" in
  major)
    major=$((major + 1)); minor=0; patch=0 ;;
  minor)
    minor=$((minor + 1)); patch=0 ;;
  patch)
    patch=$((patch + 1)) ;;
  [0-9]*\.[0-9]*\.[0-9]*)
    # Explicit version supplied
    IFS='.' read -r major minor patch <<< "$bump" ;;
  *)
    echo "Usage: $0 [major|minor|patch|X.Y.Z]" >&2
    exit 1 ;;
esac

new_version="${major}.${minor}.${patch}"

# versionCode = major*10000 + minor*100 + patch  (supports up to 99.99.99)
new_code=$(( major * 10000 + minor * 100 + patch ))

echo "Bumping $current → $new_version  (versionCode $new_code)"

# 1. Update VERSION file
printf '%s\n' "$new_version" > "$VERSION_FILE"
echo "  ✓ VERSION"

# 2. Update android/app/build.gradle
if [[ ! -f "$GRADLE_FILE" ]]; then
  echo "ERROR: $GRADLE_FILE not found." >&2; exit 1
fi
sed -i \
  -e "s/versionCode [0-9]*/versionCode $new_code/" \
  -e "s/versionName \"[^\"]*\"/versionName \"$new_version\"/" \
  "$GRADLE_FILE"
echo "  ✓ $GRADLE_FILE"

# 3. Update web/index.html APK href, download attr, and label text.
#    Matches any previous vX.Y.Z pattern so re-running is idempotent.
if [[ -f "$HTML_FILE" ]]; then
  sed -i \
    -e "s|esp32-google-maps-v[0-9]*\.[0-9]*\.[0-9]*\.apk|esp32-google-maps-v${new_version}.apk|g" \
    -e "s|APK v[0-9]*\.[0-9]*\.[0-9]*|APK v${new_version}|g" \
    -e "s|Download Android APK (v[0-9]*\.[0-9]*\.[0-9]*)|Download Android APK (v${new_version})|g" \
    "$HTML_FILE"
  echo "  ✓ $HTML_FILE"
fi

# 4. Update web/commit.json appVersion for local dev serving
if [[ -f "$COMMIT_JSON" ]]; then
  # Use Python for safe JSON manipulation; fall back to sed if unavailable.
  if command -v python3 &>/dev/null; then
    python3 -c "
import sys, json
path, ver = sys.argv[1], sys.argv[2]
with open(path) as f:
    data = json.load(f)
data['appVersion'] = ver
with open(path, 'w') as f:
    json.dump(data, f, indent=2)
    f.write('\n')
" "$COMMIT_JSON" "$new_version"
  else
    sed -i "s/\"appVersion\": \"[^\"]*\"/\"appVersion\": \"${new_version}\"/" "$COMMIT_JSON"
  fi
  echo "  ✓ $COMMIT_JSON"
fi

echo ""
echo "Next steps:"
echo "  git add VERSION android/app/build.gradle web/index.html web/commit.json"
echo "  git commit -m \"chore: Bump version to v$new_version\""
