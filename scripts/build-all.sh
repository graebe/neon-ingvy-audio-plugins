#!/usr/bin/env bash
# Build one CMake plugin project for every platform, one after the other --
# macOS natively (universal arm64 + x86_64), Linux amd64 and Windows x64 in
# their containers -- and sum up each artefact, its size, its pluginval
# verdict and the wall time it took.
#
#   scripts/build-all.sh [--juce <dir>] <cmake-project-dir>
#
# Every platform runs even after another has failed, and the exit status is
# non-zero if any did. The first Windows build needs the owner's
# XWIN_ACCEPT_LICENSE=yes (tools/docker/windows/README.md); without it the
# Windows row says so instead of a verdict.
#
# Each platform's full output is also kept, as cross-build.log in its build
# directory; the per-stage timings are in its cross-result.tsv
# (scripts/cross-common.sh describes both).
set -euo pipefail

# shellcheck source=scripts/cross-common.sh
. "$(dirname "$0")/cross-common.sh"
ni_parse_args "$@"

PLATFORMS="macos-universal:build-macos.sh linux-amd64:build-linux.sh windows-x64:build-windows.sh"
failed=0
rows=""
notes=""

for entry in $PLATFORMS; do
    id=${entry%%:*}
    script=${entry#*:}
    build="$NI_PROJECT/build-$id"
    mkdir -p "$build"
    echo
    echo "################ $id ($script)"
    start=$SECONDS
    status=0
    "$NI_ROOT/scripts/$script" --juce "$NI_JUCE" "$NI_PROJECT" 2>&1 | tee "$build/cross-build.log" || status=$?
    wall=$((SECONDS - start))
    [ "$status" = 0 ] || failed=1

    # One row per bundle; a platform that produced none gets one row naming
    # the stage that stopped it, and its notes say why, under the table.
    rows+=$(awk -F'\t' -v id="$id" -v wall="$wall" -v status="$status" '
        function size(kib) { return sprintf("%.1f MB", kib / 1024) }
        $1 == "stage" && $2 == "ctest" { ctest = $3 }
        $1 == "stage" && $3 == "FAILED" { stopped = stopped (stopped ? ", " : "") $2 }
        $1 == "bundle" { n++; path[n] = $2; kib[n] = $3; verdict[n] = $4 }
        END {
            time = sprintf("%dm %02ds", wall / 60, wall % 60)
            if (ctest == "") ctest = "not run"
            for (i = 1; i <= n; i++)
                printf "| %s | `%s` | %s | %s | %s | %s |\n", id, path[i], size(kib[i]), ctest, verdict[i], time
            if (n == 0)
                printf "| %s | none (%s) | | %s | not run | %s |\n", id,
                       (stopped ? "stopped at: " stopped : "no result, exit " status), ctest, time
        }' "$build/cross-result.tsv" 2>/dev/null || echo "| $id | none (no result file) | | | | |")
    rows+=$'\n'
    note=$(awk -F'\t' -v id="$id" '$1 == "note" { print "- " id ": " $2 }' "$build/cross-result.tsv" 2>/dev/null || true)
    [ -z "$note" ] || notes+="$note"$'\n'
done

echo
echo "| Platform | Artefact | Size | ctest | pluginval | Wall time |"
echo "|---|---|---|---|---|---|"
printf '%s' "$rows"
[ -z "$notes" ] || printf '\n%s' "$notes"
exit "$failed"
