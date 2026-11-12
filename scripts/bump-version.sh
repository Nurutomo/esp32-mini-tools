#!/usr/bin/env bash
# bump-version.sh — increment the patch version and update build.gradle
#
# Usage:
#   ./scripts/bump-version.sh           # auto-increment patch  (1.0.6 → 1.0.7)
#   ./scripts/bump-version.sh minor     # increment minor, reset patch (1.0.6 → 1.1.0)
#   ./scripts/bump-version.sh major     # increment major, reset minor+patch (1.0.6 → 2.0.0)
#   ./scripts/bump-version.sh 2.1.0     # set an explicit version
#
# After running, commit VERSION and android/app/build.gradle together.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION_FILE="$REPO_ROOT/VERSION"
GRADLE_FILE="$REPO_ROOT/android/app/build.gradle"

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

# Update VERSION file
printf '%s\n' "$new_version" > "$VERSION_FILE"

# Update android/app/build.gradle — patch the two relevant lines in-place.
if [[ ! -f "$GRADLE_FILE" ]]; then
  echo "ERROR: $GRADLE_FILE not found." >&2
  exit 1
fi

sed -i \
  -e "s/versionCode [0-9]*/versionCode $new_code/" \
  -e "s/versionName \"[^\"]*\"/versionName \"$new_version\"/" \
  "$GRADLE_FILE"

echo "Updated $GRADLE_FILE"
echo ""
echo "Next steps:"
echo "  git add VERSION android/app/build.gradle"
echo "  git commit -m \"chore: Bump version to v$new_version\""
