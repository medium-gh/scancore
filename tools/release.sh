#!/usr/bin/env bash
# Cutting a SCANCORE release, in the order the workflows expect.
#
#   tools/release.sh prepare 2.18.0 "One line for Linux software centres"
#       Sets the version everywhere it is written (CMakeLists.txt, the
#       Flatpak manifest's tag, the AppStream release notes) and checks what
#       is written by hand: the CHANGELOG.md entry ("## 2.18.0 — date") and
#       the Android store notes (fastlane/…/changelogs/<version code>.txt).
#       Commit the result as "Release SCANCORE 2.18.0: …" on main.
#
#   tools/release.sh publish 2.18.0
#       Pushes that commit to pre-release, waits for its test build (every
#       package, release.yml) and, once it passed, pushes main and the tag
#       v2.18.0, which publishes those very packages. A failed test build
#       stops here: fix, commit, publish again.
set -euo pipefail
cd "$(dirname "$0")/.."

die() { echo "release: $*" >&2; exit 1; }
[ $# -ge 2 ] || die "usage: tools/release.sh prepare <version> \"<summary>\" | publish <version>"
step=$1 version=$2
[[ $version =~ ^([0-9]+)\.([0-9]+)\.([0-9]+)$ ]] || die "$version is not a version like 2.18.0"
major=${BASH_REMATCH[1]} minor=${BASH_REMATCH[2]} patch=${BASH_REMATCH[3]}
# As CMakeLists.txt works it out (SCANCORE_ANDROID_VERSION_CODE).
code=$((major * 10000 + minor * 100 + patch + 105))
fastlane="fastlane/metadata/android/en-US/changelogs/$code.txt"

checks() {
    local ok=1
    grep -qE "^## $version( |$)" CHANGELOG.md || { echo "  CHANGELOG.md has no \"## $version — $(date +%F)\" entry"; ok=0; }
    [ -s "$fastlane" ] || { echo "  $fastlane is missing (three short lines for the Android stores)"; ok=0; }
    # As release.yml checks the extension host: one version everywhere,
    # and an Extensions runtime built from today's host/.
    local app host main
    app=$(sed -nE 's/.*HostVersion = "([^"]+)".*/\1/p' src/extensions/Runtime.h)
    host=$(sed -nE 's/^val hostVersion = "([^"]+)"\r?$/\1/p' host/desktop/build.gradle.kts)
    main=$(sed -nE 's/^const val HOST_VERSION = "([^"]+)"\r?$/\1/p' host/desktop/src/main/kotlin/com/mediumdev/scancore/host/desktop/Main.kt)
    if [ "$app" != "$host" ] || [ "$app" != "$main" ]; then
        echo "  host versions differ: Runtime.h $app, build.gradle.kts $host, Main.kt $main"; ok=0
    else
        local built
        built=$(git tag --list "extensions-runtime-$app" "extensions-runtime-$app-*" --sort=-creatordate | head -1)
        if [ -z "$built" ]; then
            echo "  no extensions-runtime-$app tag: push one and let extensions-runtime.yml publish it first"; ok=0
        elif ! git diff --quiet "$built" HEAD -- host/core host/desktop host/compat host/api host/build.gradle.kts host/settings.gradle.kts host/gradle; then
            echo "  host/ changed since $built: raise the host version and publish extensions-runtime-<new> first"; ok=0
        fi
    fi
    [ $ok = 1 ]
}

case $step in
prepare)
    summary=${3:-}
    [ -n "$summary" ] || die "prepare needs a one-line summary for the AppStream release notes"
    sed -i -E "/^project\\(/,/\\)/ s/^([[:space:]]*VERSION[[:space:]]+)[0-9]+\\.[0-9]+\\.[0-9]+/\\1$version/" CMakeLists.txt
    # The manifest's SCANCORE source (the line after its url).
    sed -i -E "/url: https:\\/\\/github.com\\/medium-gh\\/scancore.git/{n;s/tag: v[0-9.]+[^[:space:]]*/tag: v$version/}" com.mediumdev.scancore.yml
    metainfo=packaging/linux/com.mediumdev.scancore.metainfo.xml
    if ! grep -q "<release version=\"$version\"" "$metainfo"; then
        escaped=$(printf '%s' "$summary" | sed -e 's/&/\&amp;/g' -e 's/</\&lt;/g' -e 's/>/\&gt;/g' -e 's/[\/|]/\\&/g')
        sed -i "0,/<releases>/s|<releases>|<releases>\\n    <release version=\"$version\" date=\"$(date +%F)\">\\n      <description>\\n        <p>$escaped</p>\\n      </description>\\n    </release>|" "$metainfo"
    fi
    grep -qE "^[[:space:]]*VERSION[[:space:]]+$version" CMakeLists.txt || die "could not set VERSION in CMakeLists.txt"
    grep -q "tag: v$version" com.mediumdev.scancore.yml || die "could not set the Flatpak manifest's tag"
    echo "Version $version set (Android version code $code)."
    if checks; then
        echo "Ready: commit \"Release SCANCORE $version: …\" on main, then tools/release.sh publish $version."
    else
        die "write the missing parts above, then run prepare again"
    fi
    ;;
publish)
    command -v gh > /dev/null || die "publish needs the GitHub CLI (gh)"
    [ -z "$(git status --porcelain --untracked-files=no)" ] || die "commit everything first"
    [ "$(git branch --show-current)" = main ] || die "publish from main"
    grep -qE "^[[:space:]]*VERSION[[:space:]]+$version" CMakeLists.txt || die "CMakeLists.txt is not $version: run prepare"
    grep -q 'set(SCANCORE_PRERELEASE "")' CMakeLists.txt || die "SCANCORE_PRERELEASE is set: this script publishes final releases"
    git rev-parse -q --verify "refs/tags/v$version" > /dev/null && die "v$version is tagged already"
    checks || die "not ready"
    git fetch -q origin main
    git merge-base --is-ancestor origin/main HEAD || die "origin/main has commits this main has not: pull first"
    sha=$(git rev-parse HEAD)
    # pre-release only ever holds what is being tested: it follows main.
    git push --force-with-lease origin "HEAD:refs/heads/pre-release"
    echo "Waiting for the test build of ${sha:0:7}…"
    for workflow in ci.yml release.yml; do
        run=""
        for _ in $(seq 1 60); do
            run=$(gh run list --workflow "$workflow" --branch pre-release --event push --commit "$sha" --json databaseId -q '.[0].databaseId // empty')
            [ -n "$run" ] && break
            sleep 10
        done
        [ -n "$run" ] || die "no $workflow run started for $sha"
        gh run watch "$run" --exit-status --interval 60 > /dev/null || die "$workflow run $run failed: gh run view $run --log-failed"
        echo "$workflow run $run passed."
    done
    git tag -a "v$version" -m "SCANCORE $version"
    git push --atomic origin HEAD:refs/heads/main "refs/tags/v$version"
    echo "Tagged v$version: release.yml publishes test build $run's packages."
    ;;
*)
    die "unknown step $step (prepare or publish)"
    ;;
esac
