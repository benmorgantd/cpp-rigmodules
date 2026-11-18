#!/usr/bin/env bash
# UserPromptSubmit: when the prompt asks to check in or commit, tell
# Claude to run the convention-reviewer subagent first. It runs only then, so
# ordinary turns cost nothing. It is skipped when the change doesn't touch
# cpp/, python/ or tests/, and only convention-reviewer is automatic:
# simplifier and reuse-and-docs-auditor stay on request.
# The change is everything not yet on the upstream branch (unpushed commits
# plus uncommitted work), or HEAD's diff when there is no upstream. Adding
# context doesn't block the prompt.
prompt=$(jq -r '.prompt // empty')
printf '%s' "$prompt" | grep -qiE 'check[ -]?in|commit' || exit 0
cd "${CLAUDE_PROJECT_DIR:-.}" || exit 0
base=$(git rev-parse --verify -q '@{u}' || echo HEAD)
files=$( { git diff --name-only "$base"; git ls-files --others --exclude-standard; } |
	grep -E '^(cpp|python|tests)/' | sort -u)
[ -n "$files" ] || exit 0
msg="The user asked to check in or commit. Before committing, run the convention-reviewer subagent on these changed files: $(printf '%s' "$files" | tr '\n' ' '). Fix any Blocking findings, then commit and push. Do not run simplifier or reuse-and-docs-auditor unless asked."
jq -n --arg m "$msg" '{hookSpecificOutput:{hookEventName:"UserPromptSubmit",additionalContext:$m}}'
