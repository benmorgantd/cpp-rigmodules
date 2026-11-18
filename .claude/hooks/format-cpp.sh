#!/usr/bin/env bash
# PostToolUse (Edit|Write): format the C++ lines this edit touched.
# CLAUDE.md forbids whole-file formatting of existing files (it would
# re-align hand-formatted code), so a tracked file is formatted with
# git-clang-format, which only touches lines changed since HEAD. A new,
# untracked file is all new code, so it is formatted whole.
# Skips vendored headers and the generated shape library.
f=$(jq -r '.tool_response.filePath // .tool_input.file_path // empty')
[ -n "$f" ] || exit 0
case "$f" in
	*/cpp/include/*|*/cpp/core/ControlShapeLibrary.cpp) exit 0 ;;
	*/cpp/*.h|*/cpp/*.cpp) ;;
	*) exit 0 ;;
esac
cd "${CLAUDE_PROJECT_DIR:-.}" || exit 0
if git ls-files --error-unmatch -- "$f" >/dev/null 2>&1
then
	git clang-format --force --style=file -- "$f" >/dev/null
else
	clang-format -i --style=file "$f"
fi
exit 0
