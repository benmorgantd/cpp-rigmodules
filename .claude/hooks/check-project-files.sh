#!/usr/bin/env bash
# PostToolUse (Edit|Write): a cpp/ .h or .cpp must be listed in both
# RigModules.vcxproj (ClCompile/ClInclude) and RigModules.vcxproj.filters
# (with the filter for its folder), or Visual Studio won't build it.
# Two greps, so it adds no noticeable time. Exit 2 puts the message in front
# of Claude; the edit itself is already done.
f=$(jq -r '.tool_response.filePath // .tool_input.file_path // empty')
case "$f" in
	*/cpp/include/*) exit 0 ;;
	*/cpp/*.h|*/cpp/*.cpp) ;;
	*) exit 0 ;;
esac
cd "${CLAUDE_PROJECT_DIR:-.}/cpp" || exit 0
rel=${f#*/cpp/}
win=${rel//\//\\}
dir=$(dirname "$rel")
[ "$dir" = "." ] && dir=""
windir=${dir//\//\\}
kind=ClCompile
case "$rel" in *.h) kind=ClInclude ;; esac
problems=""
grep -qF "<$kind Include=\"$win\"" RigModules.vcxproj ||
	problems="$problems\n- RigModules.vcxproj has no <$kind Include=\"$win\" />."
if ! awk -v tag="<$kind Include=\"$win\"" -v want="<Filter>$windir</Filter>" '
	index($0, tag) { found = 1; next }
	found { if (index($0, want)) ok = 1; exit }
	END { exit ok ? 0 : 1 }' RigModules.vcxproj.filters
then
	if [ -n "$windir" ]
	then
		problems="$problems\n- RigModules.vcxproj.filters has no <$kind Include=\"$win\"> with <Filter>$windir</Filter>."
	elif ! grep -qF "<$kind Include=\"$win\"" RigModules.vcxproj.filters
	then
		problems="$problems\n- RigModules.vcxproj.filters has no <$kind Include=\"$win\" />."
	fi
fi
[ -z "$problems" ] && exit 0
printf "cpp/%s is not fully registered in the Visual Studio project:%b\nAdd it to both files (CLAUDE.md, repo map).\n" "$rel" "$problems" >&2
exit 2
