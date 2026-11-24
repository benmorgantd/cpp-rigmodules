"""PreToolUse hook for Bash and PowerShell: blocks the command shapes that kept failing (see .claude/rules/shell.md).

Reads the hook JSON on stdin. Exit 2 with a message on stderr blocks the call and shows the message to the model.
Exit 0 lets it through. Any problem inside the hook itself lets the call through.
"""
import json
import os
import re
import shlex
import sys
import warnings

MAX_HEREDOC_COMMAND = 7000  # none failed under 7000 (about 1,550 runs); from 7200 up they mostly did


def heredoc_bodies(command):
    """(interpreter line, body) for each heredoc in the command."""
    out = []
    for m in re.finditer(r"<<-?\s*(['\"]?)(\w+)\1[^\n]*\n(.*?)\n\s*\2\s*(?:\n|$)", command, re.S):
        line_start = command.rfind("\n", 0, m.start()) + 1
        out.append((command[line_start:m.start()], m.group(3)))
    return out


def invalid_escape(body):
    try:
        with warnings.catch_warnings():
            warnings.simplefilter("error", SyntaxWarning)
            compile(body, "<heredoc>", "exec")
    except (SyntaxWarning, SyntaxError) as w:
        # compile() turns a warning that is an error into a SyntaxError; a real syntax error is the script's own problem.
        return str(w) if "invalid escape" in str(w) else None
    except Exception:
        return None  # a plain syntax error is the script's own problem
    return None


def sed_in_place_on_crlf(command, cwd):
    for part in re.split(r"[;&|\n]", command):
        if not re.search(r"\bsed\b", part) or not re.search(r"\s(-[a-zA-Z]*i\S*|--in-place\S*)(\s|$)", part):
            continue
        try:
            tokens = shlex.split(part, posix=True)
        except ValueError:
            continue
        for t in tokens[1:]:
            if t.startswith("-"):
                continue
            path = t if os.path.isabs(t) or re.match(r"^/[a-z]/", t) else os.path.join(cwd, t)
            if re.match(r"^/[a-z]/", path):
                path = path[1].upper() + ":" + path[2:]
            try:
                with open(path, "rb") as f:
                    if b"\r\n" in f.read(200000):
                        return path
            except OSError:
                pass
    return None


def check(tool, command, cwd):
    if tool == "Bash":
        bodies = heredoc_bodies(command)
        if bodies and len(command) > MAX_HEREDOC_COMMAND:
            return ("This command is %d characters with a heredoc. Over about 7000 it fails with 'unexpected EOF "
                    "while looking for matching'. Write the file with the Write tool (scratchpad for a throwaway "
                    "script), then run it. See .claude/rules/shell.md." % len(command))
        for head, body in bodies:
            if re.search(r"\bpython3?\b|\bpy\b", head):
                if re.search(r"""replace\(\s*(['"])\\n\1\s*,\s*(['"])\\r\\n\2""", body):
                    return ("This Python heredoc converts \\n to \\r\\n to edit a source file. The Edit tool keeps "
                            "CRLF itself: use Edit. See .claude/rules/shell.md.")
                w = invalid_escape(body)
                if w:
                    return ("Python reports '%s' in this heredoc. \\r, \\n and \\t in a plain string silently become "
                            "control characters. Use a raw string (r\"...\") for text with backslashes, or the Edit "
                            "tool." % w)
        p = sed_in_place_on_crlf(command, cwd)
        if p:
            return "sed -i strips the CR from %s (a CRLF file). Use the Edit tool." % p
    if tool == "PowerShell":
        if re.search(r"mayapy|MSBuild", command, re.I) and "2>&1" in command and "cmd /c" not in command.lower():
            return ("2>&1 on a native exe in PowerShell wraps stderr in NativeCommandError traces. Run mayapy from the "
                    "Bash tool (... 2>&1 | tail -40), or drop the redirect. See .claude/rules/shell.md.")
    return None


def main():
    try:
        data = json.load(sys.stdin)
        message = check(data.get("tool_name"), data.get("tool_input", {}).get("command", ""), data.get("cwd") or os.getcwd())
    except Exception:
        return 0
    if message:
        sys.stderr.write(message + "\n")
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
