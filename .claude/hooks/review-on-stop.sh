#!/usr/bin/env bash
# Stop: when the turn leaves uncommitted changes, ask Claude to run the
# simplifier subagent, then the convention-reviewer subagent, on them.
# Hooks cannot start subagents themselves, so this blocks the stop once with
# instructions. stop_hook_active is true on the follow-up stop: let it end,
# or the two agents would loop forever.
input=$(cat)
[ "$(printf '%s' "$input" | jq -r '.stop_hook_active // false')" = "true" ] && exit 0
cd "${CLAUDE_PROJECT_DIR:-.}" || exit 0
files=$(git status --porcelain -- . ':!.claude' | sed 's/^...//')
[ -n "$files" ] || exit 0
reason="Uncommitted changes in: $(printf '%s' "$files" | tr '\n' ' '). Run the simplifier subagent on them. After it finishes, run the convention-reviewer subagent on the result. Report both outputs and fix any Blocking findings. If the changes are only unrelated leftovers from earlier turns, say so and skip."
jq -n --arg r "$reason" '{decision:"block", reason:$r}'
