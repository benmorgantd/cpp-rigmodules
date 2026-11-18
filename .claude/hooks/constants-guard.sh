#!/usr/bin/env bash
# PostToolUse (Edit|Write): core/Constants.h holds the shared names (RigAttr,
# JointMarkupKey, PlugAndSocketNames, ConstraintTypes, ...). Names are public
# API (CLAUDE.md), so when a line there was changed or removed, remind Claude
# to state the impact. Added lines alone stay quiet. Non-blocking: the reminder
# goes in as context.
f=$(jq -r '.tool_response.filePath // .tool_input.file_path // empty')
case "$f" in
	*/cpp/core/Constants.h) ;;
	*) exit 0 ;;
esac
cd "${CLAUDE_PROJECT_DIR:-.}" || exit 0
git diff -U0 HEAD -- cpp/core/Constants.h | grep -q '^-[^-]' || exit 0
msg="cpp/core/Constants.h had existing lines changed or removed. These names are public API. In your summary, state which names changed and the impact on saved poses, animation, shape files, templates and exports, and whether a format update step (D33) is needed. Add or update the decision in design/decisions.md."
jq -n --arg m "$msg" '{hookSpecificOutput:{hookEventName:"PostToolUse",additionalContext:$m}}'
