#!/usr/bin/env python3
"""Engine regression tests: hub templates and targeted feature checks, run through the editor's automation.

usage: regression.py [--list] [case ...]       (no cases = everything)

Cases:
  template:<id>   generated for every hub template: the hub creates a fresh project, the editor opens it, presses Play
                  for 240 frames and stops. Passes with 0 script errors.
  <name>          Tools/Tests/cases/<name>.txt: an editor automation script. Header lines:
                      # template: <hub template id>   the project the case runs on (made fresh with the hub)
                  "{dir}" in the script is replaced by Tools/Tests/cases (checks/*.cqs live there). A check script
                  creates an entity named RegressionCheckOK when its condition holds; the case then expects it.

Every case must end with "DONE failures=0" and the editor must exit with code 0 (a crash while shutting down, e.g. a
GPU resource released after the GL context, is a failure too).

Environment (headless machines need a compositor, see Docs/Testing.md):
  CQ_RUNNER      wrapper "<automation file> <project dir>" that runs the editor and prints ENGINE_EXIT=<code>
  CQ_HUB_RUNNER  wrapper "<automation file>" that runs the hub (ConquerorLauncher)
Projects are made in ~/.cache/cq-build/regression. Game saves go to XDG_DATA_HOME (set it to keep tests away from
your own saves).
"""
import os
import re
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
BIN = os.path.join(ROOT, "build", "bin")
CASES = os.path.join(HERE, "cases")
WORK = os.path.join(os.path.expanduser("~"), ".cache", "cq-build", "regression")


def hub_templates():
    # Hub şablon kimlikleri: Launcher/src/HubTemplates.cpp listesindeki { "id", "Name", "Category", ... } satırları
    src = open(os.path.join(ROOT, "Launcher", "src", "HubTemplates.cpp"), encoding="utf-8").read()
    body = src[src.index("GetTemplates()"):]
    return re.findall(r'\{\s*"([a-z0-9]+)",\s*"[^"]+",\s*"(?:Core|Sample|Test)"', body)


def run(cmd, env=None, cwd=None):
    p = subprocess.run(cmd, env=env, cwd=cwd, capture_output=True, text=True, check=False)
    return p.returncode, (p.stdout or "") + (p.stderr or "")


def make_project(tid, name):
    os.makedirs(WORK, exist_ok=True)
    project = os.path.join(WORK, name)
    if os.path.isdir(project):
        shutil.rmtree(project)
    script = os.path.join(WORK, f"_hub_{name}.txt")
    with open(script, "w") as f:
        f.write(f'wait 20\nhub create {tid} {name} "{WORK}"\nwait 5\nquit\n')
    runner = os.environ.get("CQ_HUB_RUNNER")
    if runner:
        run(["bash", runner, script])
    else:
        run([os.path.join(BIN, "ConquerorLauncher")], env=dict(os.environ, CQ_AUTOMATION=script), cwd=BIN)
    return project if os.path.isdir(project) else None


def run_editor(script, project):
    if os.path.exists(script + ".result.txt"):
        os.remove(script + ".result.txt")
    runner = os.environ.get("CQ_RUNNER")
    if runner:
        code, out = run(["bash", runner, script, project])
        m = re.search(r"ENGINE_EXIT=(\d+)", out)
        code = int(m.group(1)) if m else code
    else:
        code, out = run([os.path.join(BIN, "ConquerorEngine"), project], env=dict(os.environ, CQ_AUTOMATION=script), cwd=BIN)
    result = open(script + ".result.txt").read() if os.path.exists(script + ".result.txt") else ""
    return code, result


def case_script(name):
    text = open(os.path.join(CASES, name + ".txt"), encoding="utf-8").read()
    m = re.search(r"^#\s*template:\s*(\S+)", text, re.M)
    return (m.group(1) if m else "empty"), text.replace("{dir}", CASES)


def template_script():
    return "\n".join(["wait 30", "focus Game", "script_errors mark", "play", "wait 240", "script_errors expect 0", "stop", "quit"]) + "\n"


def run_case(case):
    start = time.time()
    if case.startswith("template:"):
        tid = case.split(":", 1)[1]
        text = template_script()
    else:
        tid, text = case_script(case)
    name = "Reg_" + re.sub(r"[^A-Za-z0-9_]", "_", case)
    project = make_project(tid, name)
    if not project:
        return False, f"hub could not create a '{tid}' project", time.time() - start
    script = os.path.join(WORK, f"_{name}.txt")
    with open(script, "w", encoding="utf-8") as f:
        f.write(text)
    code, result = run_editor(script, project)
    fails = [l for l in result.splitlines() if " FAIL " in l or l.startswith("FAIL")]
    done = re.search(r"DONE failures=(\d+)", result)
    if not done:
        return False, "automation did not finish (crash or timeout)", time.time() - start
    if int(done.group(1)) != 0:
        return False, "; ".join(fails)[:300] or f"{done.group(1)} failures", time.time() - start
    if code != 0:
        return False, f"editor exit code {code} (crash at shutdown?)", time.time() - start
    return True, "", time.time() - start


def all_cases():
    named = sorted(f[:-4] for f in os.listdir(CASES) if f.endswith(".txt"))
    return [f"template:{t}" for t in hub_templates()] + named


def main():
    args = sys.argv[1:]
    if "--list" in args:
        print("\n".join(all_cases()))
        return
    cases = args or all_cases()
    results = []
    for c in cases:
        ok, why, secs = run_case(c)
        results.append((c, ok, why))
        print(f"{'PASS' if ok else 'FAIL'}  {c:34s} {secs:6.1f} s  {why}", flush=True)
    failed = [r for r in results if not r[1]]
    print(f"\n{len(results) - len(failed)} / {len(results)} passed")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
