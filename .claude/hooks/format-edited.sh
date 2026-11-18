#!/usr/bin/env bash
# PostToolUse (Edit|Write): format only what this edit touched.
# CLAUDE.md forbids whole-file formatting of existing files (it would
# re-align hand-formatted code), so a tracked file is formatted on the lines
# changed since HEAD. A new, untracked file is all new code and is formatted
# whole.
#   cpp/ .h .cpp  clang-format with cpp/.clang-format (git-clang-format for
#                 tracked files). Skips vendored headers and the generated
#                 shape library.
#   .py           black at 120 columns, limited by --line-ranges to the
#                 changed hunks. Most existing files don't match black, so a
#                 whole-file run would rewrite them.
#   .json         validated only. clang-format reflows a JSON file's whole
#                 structure (tested: it exploded the inline arrays in
#                 RigTemplates/), and the templates and pose fixtures are
#                 hand-formatted. An invalid file exits 2 so Claude sees it.
f=$(jq -r '.tool_response.filePath // .tool_input.file_path // empty')
[ -n "$f" ] || exit 0
cd "${CLAUDE_PROJECT_DIR:-.}" || exit 0
tracked=false
git ls-files --error-unmatch -- "$f" >/dev/null 2>&1 && tracked=true

case "$f" in
	*/cpp/include/*|*/cpp/core/ControlShapeLibrary.cpp) exit 0 ;;
	*/cpp/*.h|*/cpp/*.cpp)
		if $tracked
		then
			git clang-format --force --style=file -- "$f" >/dev/null
		else
			clang-format -i --style=file "$f"
		fi
		;;
	*.py)
		command -v black >/dev/null || exit 0
		if $tracked
		then
			ranges=$(git diff -U0 HEAD -- "$f" | awk '/^@@/ {
				split($3, p, ","); start = substr(p[1], 2); n = (p[2] == "" ? 1 : p[2])
				if (n > 0) printf "--line-ranges=%d-%d\n", start, start + n - 1 }')
			[ -n "$ranges" ] || exit 0
			# shellcheck disable=SC2086
			black -q -l 120 $ranges "$f"
		else
			black -q -l 120 "$f"
		fi
		;;
	*.json)
		if ! err=$(jq empty "$f" 2>&1)
		then
			echo "Invalid JSON in $f: $err" >&2
			exit 2
		fi
		;;
esac
exit 0
