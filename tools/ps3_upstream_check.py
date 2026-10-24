#!/usr/bin/env python3
"""Reports what merging OpenCE's main (remote `upstream`) into the PS3 port takes; --merge also merges and builds it in
../PS3-upstream-check, never in this tree. Exit status: 0 nothing to merge, 1 a merge waits, 2 the check failed."""

import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
REMOTE = "upstream"
BRANCH = "main"
WORKTREE = ROOT.parent / "PS3-upstream-check"
CHECK_BRANCH = "upstream-check"
BUILD = "build/ps3_upstream_check"
# the declarations of the functions the game's sources and the shared units call outside them
HOOK_HEADERS = ("port/linux/include/", "port/linux/src/")
PROTOTYPE = re.compile(r"^\+\s*(?:extern\s+)?(?:const\s+)?(?:unsigned\s+|signed\s+|struct\s+)?\w+[\s\*]+(\w+)\s*\([^;]*\)\s*;")
MESSAGE = re.compile(r"^\+\s*(_distributed_message_\w+)\s*=\s*(\d+)")
VERSION = re.compile(r"#define\s+HALO_PORT_NETWORK_VERSION\s+(\d+)")
STRUCT = re.compile(r"struct\s+(\w+)\s*\{([^{}]*)\}\s*(\w*)\s*(?:\[[^\]]*\])?\s*;", re.S)
POINTER_MEMBER = re.compile(r"^\s*(?!typedef)[\w\s]+\*\s*(?!HALO_PTR32)(\w+)\s*(?:\[[^\]]*\])?\s*;", re.M)
BLOCK_COMMENT = re.compile(r"/\*.*?(?:\*/|$)", re.S)
LINE_COMMENT = re.compile(r"//[^\n]*")
SPACE = re.compile(r"\s+")


def git(*arguments, cwd=ROOT, check=True, stdin=None):
    # core.autocrlf=false: the tree is LF, the system's git writes CRLF
    result = subprocess.run(["git", "-c", "core.autocrlf=false", *arguments], cwd=cwd, capture_output=True,
                            text=True, encoding="utf-8", errors="replace", input=stdin)
    if check and result.returncode != 0:
        raise RuntimeError(f"git {' '.join(arguments)}: {result.stderr.strip()}")
    return result


def bash():
    found = shutil.which("bash") or shutil.which("sh")
    if found:
        return found
    git_bash = Path("C:/Program Files/Git/bin/bash.exe")
    if git_bash.exists():
        return str(git_bash)
    raise RuntimeError("no bash for build.sh")


def conflicts(start, upstream):
    # --name-only: the tree, the conflicting paths, a blank line, the messages
    result = git("merge-tree", "--write-tree", "--name-only", start, upstream, check=False)
    if result.returncode not in (0, 1):
        raise RuntimeError(f"git merge-tree: {result.stderr.strip()}")
    names = []
    for line in result.stdout.splitlines()[1:]:
        if not line:
            break
        names.append(line)
    return names


def network_version(revision):
    text = git("show", f"{revision}:port/linux/include/halo_port_limits.h", check=False).stdout
    match = VERSION.search(text)
    return int(match.group(1)) if match else None


def added_lines(base, upstream, *paths):
    return git("diff", "-U0", base, upstream, "--", *paths).stdout.splitlines()


def new_messages(base, upstream):
    return [(name, int(number)) for line in added_lines(base, upstream, "port/linux/game/network_distributed.h")
            if (match := MESSAGE.match(line)) for name, number in [match.groups()]]


def new_hooks(base, upstream):
    hooks = []
    path = None
    for line in added_lines(base, upstream, *HOOK_HEADERS):
        if line.startswith("+++ "):
            path = line[6:] if line.startswith("+++ b/") else None
        elif path and path.endswith(".h") and (match := PROTOTYPE.match(line)):
            hooks.append((path, match.group(1)))
    return hooks


def pointer_structs(base, upstream):
    found = []
    changed = git("diff", "--name-only", "--diff-filter=AM", base, upstream, "--", "port/linux/game",
                  "source").stdout.split("\n")
    for path in filter(None, changed):
        if not path.endswith((".c", ".h")):
            continue
        new_text = git("show", f"{upstream}:{path}", check=False).stdout
        old_text = git("show", f"{base}:{path}", check=False).stdout
        old = {match.group(1) or match.group(3): match.group(2) for match in STRUCT.finditer(old_text)}
        for match in STRUCT.finditer(new_text):
            name = match.group(1) or match.group(3)
            if old.get(name) == match.group(2):
                continue
            members = POINTER_MEMBER.findall(match.group(2))
            if members:
                found.append((path, name, members))
    return found


def code_only(lines):
    """A conflict side's C code without its comments and spaces (a side can begin or end inside a comment)."""
    text = "\n".join(lines)
    first_end, first_open = text.find("*/"), text.find("/*")
    if first_end != -1 and (first_open == -1 or first_end < first_open):
        text = text[first_end + 2:]
    return SPACE.sub("", LINE_COMMENT.sub(" ", BLOCK_COMMENT.sub(" ", text)))


def resolve_comment_conflicts(path):
    """Upstream's side of each conflict (diff3 markers) where ours differs from the base in comments only; returns
    how many conflicts were taken and how many are left."""
    lines = path.read_text(encoding="utf-8", errors="surrogateescape").split("\n")
    out, taken, left, index = [], 0, 0, 0
    while index < len(lines):
        if not lines[index].startswith("<<<<<<< "):
            out.append(lines[index])
            index += 1
            continue
        parts, side, end = {"ours": [], "base": [], "theirs": []}, "ours", index + 1
        while end < len(lines) and not lines[end].startswith(">>>>>>> "):
            if lines[end].startswith("||||||| "):
                side = "base"
            elif lines[end] == "=======":
                side = "theirs"
            else:
                parts[side].append(lines[end])
            end += 1
        if end < len(lines) and code_only(parts["ours"]) == code_only(parts["base"]):
            out += parts["theirs"]
            taken += 1
        else:
            out += lines[index:end + 1]
            left += 1
        index = end + 1
    with open(path, "w", encoding="utf-8", errors="surrogateescape", newline="") as output:
        output.write("\n".join(out))
    return taken, left


def prepare_worktree(start):
    if WORKTREE.exists():
        git("merge", "--abort", cwd=WORKTREE, check=False)
        git("checkout", "-q", "-f", "-B", CHECK_BRANCH, start, cwd=WORKTREE)
        git("reset", "-q", "--hard", start, cwd=WORKTREE)
    else:
        git("worktree", "add", "-q", "-B", CHECK_BRANCH, str(WORKTREE), start)
    # the PS3's units the .gitignore's xbox/ hides, if not added with git add -f
    for name in git("ls-files", "--others", "--ignored", "--exclude-standard", "--", "port/ps3").stdout.split("\n"):
        if name and "/build/" not in name:
            (WORKTREE / name).parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / name, WORKTREE / name)


def run_build(target, report):
    log = WORKTREE / BUILD / f"upstream_check_{target}.log"
    log.parent.mkdir(parents=True, exist_ok=True)
    with open(log, "w", encoding="utf-8") as output:
        status = subprocess.run([bash(), "build.sh", "-j12", "-k", f"BUILD={BUILD}", target], cwd=WORKTREE,
                                stdout=output, stderr=subprocess.STDOUT).returncode
    text = log.read_text(encoding="utf-8", errors="replace")
    errors = sorted(set(re.findall(r"^(\S+:\d+:\d+: error: .*)$", text, re.M)))
    undefined = sorted(set(re.findall(r"undefined reference to `([^']+)'", text)))
    report.append(f"Build {target}: {'passed' if status == 0 else 'failed'} (the log: {log})")
    report += [f"  {line}" for line in re.findall(r"^(r2off stubs:.*|\.got size:.*|shader pack:.*)$", text, re.M)]
    report += [f"  {line}" for line in errors[:60]]
    if undefined:
        report.append("  undefined (each a hook the PS3 must give: port/ps3/xbox/port_stubs.c, or code):")
        report += [f"    {name}" for name in undefined]
    return status == 0


def merge(start, upstream, head, report):
    """The merge in the worktree (comment-only conflicts: upstream's side), committed there, then check and pkg."""
    prepare_worktree(start)
    merged = git("-c", "merge.conflictStyle=diff3", "merge", "--no-ff", "--no-commit", upstream, cwd=WORKTREE,
                 check=False)
    taken_files, left_files = [], []
    for line in git("status", "--porcelain", cwd=WORKTREE).stdout.splitlines():
        state, name = line[:2], line[3:]
        if state == "DU":
            # deleted on purpose here (as .github/workflows/build.yml), changed upstream: it stays deleted
            git("rm", "-q", "--", name, cwd=WORKTREE)
            taken_files.append(f"{name} (kept deleted)")
        elif state == "UU" and name.endswith((".c", ".h")):
            taken, left = resolve_comment_conflicts(WORKTREE / name)
            if left:
                left_files.append(f"{name} ({left} conflicts left, {taken} comment-only taken from upstream)")
            else:
                git("add", "--", name, cwd=WORKTREE)
                taken_files.append(f"{name} ({taken} comment-only)")
        elif state in ("UU", "AA", "UD", "AU", "UA", "DD"):
            left_files.append(f"{name} ({state})")
    if taken_files:
        report.append("Conflicts resolved with upstream's side:")
        report += [f"  {name}" for name in taken_files]
    if left_files:
        report.append(f"Merge: stopped in {WORKTREE} (branch {CHECK_BRANCH}); these files need a person:")
        report += [f"  {name}" for name in left_files]
        return
    if merged.returncode != 0 and not taken_files:
        report.append("Merge: failed:\n  " + (merged.stdout + merged.stderr).strip().replace("\n", "\n  "))
        return
    message = f"Merge OpenCE upstream ({head}) into the PS3 port\n\nMade by tools/ps3_upstream_check.py --merge."
    if taken_files:
        message += " Conflicts taken from upstream:\n" + "\n".join(f"- {name}" for name in taken_files)
    git("commit", "-q", "-F", "-", cwd=WORKTREE, stdin=message + "\n")
    commit = git("rev-parse", "--short", "HEAD", cwd=WORKTREE).stdout.strip()
    report.append(f"Merge: {commit} on {CHECK_BRANCH} in {WORKTREE}")
    if run_build("check", report) and run_build("pkg", report):
        report.append(f"PKG: {WORKTREE / BUILD / 'halo_ps3.pkg'}. After a PS3 test, with main's tree clean and no"
                      f" shader sweep running: git merge --ff-only {CHECK_BRANCH}")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--report", type=Path, help="also write the report to this file")
    parser.add_argument("--merge", action="store_true",
                        help="merge in ../PS3-upstream-check (comment-only conflicts: upstream's), then check and pkg")
    parser.add_argument("--start", default="HEAD", help="the commit to merge into (for a test: an older one)")
    args = parser.parse_args()

    git("fetch", "-q", REMOTE, BRANCH)
    upstream = f"{REMOTE}/{BRANCH}"
    head = git("rev-parse", "--short", upstream).stdout.strip()
    start = git("rev-parse", args.start).stdout.strip()
    base = git("merge-base", start, upstream).stdout.strip()
    waiting = int(git("rev-list", "--count", f"{start}..{upstream}").stdout)
    report = [f"OpenCE {upstream} at {head}: {waiting} commits not merged (merged up to {base[:8]})"]
    if waiting == 0:
        print(report[0])
        if args.report:
            args.report.write_text(report[0] + "\n", encoding="utf-8")
        return 0

    names = conflicts(start, upstream)
    report.append(f"Conflicts: {len(names)} files")
    report += [f"  {name}" for name in names]
    old_version, new_version = network_version(base), network_version(upstream)
    if old_version != new_version:
        report.append(f"Network version: {old_version} to {new_version} (port/linux/NETCODE.md says what changed: the"
                      " PS3's byte swaps of each changed message)")
    messages = new_messages(base, upstream)
    if messages:
        report.append("New distributed messages (a case in network_distributed.c's distributed_message_entries_byte_swap):")
        report += [f"  {number} {name}" for name, number in messages]
    hooks = new_hooks(base, upstream)
    if hooks:
        report.append("New declarations in the shared headers (a PS3 definition if the game calls them):")
        report += [f"  {name} ({path})" for path, name in hooks]
    structs = pointer_structs(base, upstream)
    if structs:
        report.append("New or changed structs with pointers (HALO_PTR32 if they mirror the Xbox's 32-bit data):")
        report += [f"  {path}: struct {name}: {', '.join(members)}" for path, name, members in structs]
    rasterizer = git("diff", "--name-only", base, upstream, "--", "source/rasterizer").stdout.split()
    if rasterizer:
        report.append(f"Rasterizer: {len(rasterizer)} files changed: port/ps3/shader_keys.txt needs a new sweep")
    if args.merge:
        merge(start, upstream, head, report)

    text = "\n".join(report)
    print(text)
    if args.report:
        args.report.write_text(text + "\n", encoding="utf-8")
    return 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except RuntimeError as error:
        print(f"ps3_upstream_check: {error}", file=sys.stderr)
        sys.exit(2)
