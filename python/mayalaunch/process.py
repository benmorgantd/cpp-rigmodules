"""Starts a Maya process, watches it, and cleans up after a crash. Plain Python: nothing here imports Maya."""
import os
import shutil
import subprocess
import sys
from dataclasses import dataclass

from mayalaunch import install

# Processes Maya's crash handler starts. Disabled through MAYA_DISABLE_CER, and closed after a crash in case.
CRASH_REPORTER_PROCESSES = ("senddmp.exe", "cer_dialog.exe")


@dataclass
class ProcessResult:
    exit_code: int
    timed_out: bool
    pid: int
    closed_reporters: int

    @property
    def ok(self):
        return self.exit_code == 0 and not self.timed_out


def child_environment(work_dir=None, skip_usersetup=True, extra=None):
    """The environment for a Maya child.

    MAYA_DISABLE_CER stops the crash report dialog. skip_usersetup keeps the user's userSetup.py out, so a
    test sees only what it loads. With work_dir, TEMP and TMP point inside it, so Maya's temp files and
    crash dumps stay in the run folder."""
    env = dict(os.environ)
    env["MAYA_DISABLE_CER"] = "1"
    env["PYTHONDONTWRITEBYTECODE"] = "1"
    if skip_usersetup:
        env["MAYA_SKIP_USERSETUP_PY"] = "1"
    if work_dir:
        maya_temp = os.path.join(work_dir, "maya_temp")
        os.makedirs(maya_temp, exist_ok=True)
        env["TEMP"] = maya_temp
        env["TMP"] = maya_temp
    env.update({key: str(value) for key, value in (extra or {}).items()})
    return env


def copy_plugin(plugin, dest_dir):
    """Copies a plugin into dest_dir and returns the copy. A loaded plugin locks its file, so Maya loads the
    copy and the build output stays free to rebuild."""
    os.makedirs(dest_dir, exist_ok=True)
    copy = os.path.join(dest_dir, os.path.basename(plugin))
    shutil.copyfile(plugin, copy)
    return copy


def close_crash_reporters(pid):
    """Closes crash reporter processes started by pid or its descendants. Returns how many were closed.
    Matches on parent process id, so the user's own Maya sessions are never touched. Windows only."""
    if sys.platform != "win32":
        return 0
    script = (
        "$ids = @(%d); $found = @();"
        "do { $next = Get-CimInstance Win32_Process | Where-Object { $ids -contains $_.ParentProcessId -and $found -notcontains $_.ProcessId };"
        " $found += $next.ProcessId; $ids = $next.ProcessId } while ($next);"
        "$found | Where-Object { $_ } | ForEach-Object { Get-Process -Id $_ -ErrorAction SilentlyContinue } |"
        " Where-Object { @('%s') -contains ($_.ProcessName + '.exe') } |"
        " ForEach-Object { Stop-Process -Id $_.Id -Force; $_.Id }"
    ) % (pid, "','".join(CRASH_REPORTER_PROCESSES))
    output = subprocess.run(["powershell", "-NoProfile", "-NonInteractive", "-Command", script],
                            capture_output=True, text=True).stdout
    return len([line for line in output.splitlines() if line.strip()])


def kill_process_tree(pid):
    """Stops pid and everything it started. Only this process's tree, never Maya by name."""
    if sys.platform == "win32":
        subprocess.run(["taskkill", "/PID", str(pid), "/T", "/F"], capture_output=True)
    else:
        os.kill(pid, 9)


def run_watched(command, env=None, timeout=None, cwd=None):
    """Runs a command to the end. On timeout the process tree is stopped. After a crash or timeout the
    crash reporters it left open are closed. Output is not captured: it goes where the caller's goes."""
    child = subprocess.Popen(command, env=env, cwd=cwd)
    timed_out = False
    try:
        exit_code = child.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        timed_out = True
        kill_process_tree(child.pid)
        exit_code = child.wait()
    closed = close_crash_reporters(child.pid) if (exit_code != 0 or timed_out) else 0
    return ProcessResult(exit_code, timed_out, child.pid, closed)


def run_mayapy(script, args=(), env=None, timeout=None, maya_dir=None, cwd=None):
    """Runs a script in a new mayapy. The environment defaults to child_environment()."""
    command = [install.mayapy_exe(maya_dir), script, *args]
    return run_watched(command, env if env is not None else child_environment(), timeout, cwd)


def start_mayapy(script, args=(), env=None, maya_dir=None, cwd=None, log_path=None):
    """Starts a script in a new mayapy and returns its Popen without waiting, for a caller that watches several
    at once. With log_path, its stdout and stderr (Maya's own output included) go to that file. The caller
    stops it with kill_process_tree and closes the crash reporters with close_crash_reporters."""
    command = [install.mayapy_exe(maya_dir), script, *args]
    log = open(log_path, "w", encoding="utf-8") if log_path else None
    try:
        return subprocess.Popen(command, env=env if env is not None else child_environment(), cwd=cwd,
                                stdout=log, stderr=subprocess.STDOUT if log else None)
    finally:
        if log:
            log.close()
