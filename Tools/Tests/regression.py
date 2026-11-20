#!/usr/bin/env python3
"""Engine regression tests: hub templates and targeted feature checks, run through the editor's automation.

usage: regression.py [--list] [-j N] [case ...]       (no cases = everything; -j N runs N editors at the same time)

Cases:
  template:<id>   generated for every hub template: the hub creates a fresh project, the editor opens it, presses Play
                  for 240 frames and stops. Passes with 0 script errors.
  sample:<name>   generated for every project in Samples/: a fresh copy is opened and every scene is played for 90
                  frames. Passes with 0 script errors in every scene (the UI widgets are created, scripts find them).
  <name>          Tools/Tests/cases/<name>.txt: an editor automation script. Header lines:
                      # template: <hub template id>   the project the case runs on (made fresh with the hub)
                      # serial                         timing-sensitive: runs alone after the parallel cases (-j)
                      # requires: VAR                  runs only when environment variable VAR is set (else SKIP)
                      # env: K=V K2=V2                 extra environment for the editor of this case
                      # matrix: VAR=v1 v2 ...          runs the case once per value as <name>@<value> with VAR=<value>;
                                                       "{matrix}" in the script is replaced by the value
                      # hub                            the script runs in the hub (ConquerorLauncher) with its own fresh
                                                       config folder instead of in the editor (no project is made)
                      # setup: <command>               runs before the case (e.g. a fixture generator from cases/assets)
                      # teardown: <command>            runs after the case, also when it failed ("{bin}": the binaries)
                      # check: <command>               runs after a passing case; a non-zero exit fails the case
                                                       (e.g. start the built game with the player)
                  "{dir}" in the script is replaced by Tools/Tests/cases (checks/*.cqs live there), "{work}" by the
                  regression work folder. A check script
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
import shlex
import shutil
import subprocess
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor, as_completed

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
# CQ_REGRESSION_BIN: build/bin'in bir kopyası (regresyon arka planda sürerken yeniden derleme sonuçları bozmasın)
BIN = os.environ.get("CQ_REGRESSION_BIN") or os.path.join(ROOT, "build", "bin")
# Testler hoparlörden ses çıkarmasın: motor miniaudio'nun sessiz null aygıtını kullanır (saat yine gerçek zamanda ilerler)
os.environ.setdefault("CQ_AUDIO_NULL", "1")
CASES = os.path.join(HERE, "cases")
WORK = os.path.join(os.path.expanduser("~"), ".cache", "cq-build", "regression")


def hub_templates():
    # Hub şablon kimlikleri: Launcher/src/HubTemplates.cpp listesindeki { "id", "Name", "Category", ... } satırları
    src = open(os.path.join(ROOT, "Launcher", "src", "HubTemplates.cpp"), encoding="utf-8").read()
    body = src[src.index("GetTemplates()"):]
    return re.findall(r'\{\s*"([a-z0-9]+)",\s*"[^"]+",\s*"(?:Core|Sample|Test)"', body)


def run(cmd, env=None, cwd=None):
    # Paralel çalışmada (-j) her işçinin ortamı (kendi oyun kayıt klasörü) run_case'den gelir
    p = subprocess.run(cmd, env=env, cwd=cwd, capture_output=True, text=True, check=False)
    return p.returncode, (p.stdout or "") + (p.stderr or "")


def samples():
    # Samples/<Ad>/<Ad>.cqproj olan örnek projeler (Parkour, Ember Trail, Tetris)
    base = os.path.join(ROOT, "Samples")
    if not os.path.isdir(base):
        return []
    return sorted(d for d in os.listdir(base) if os.path.isfile(os.path.join(base, d, d + ".cqproj")))


def sample_project(sample, name):
    # Örneğin taze kopyası (depodaki dosyalara ve kullanıcının açtığı kopyaya dokunulmaz)
    os.makedirs(WORK, exist_ok=True)
    project = os.path.join(WORK, name)
    if os.path.isdir(project) and os.path.basename(project).startswith("Reg_sample_"):
        shutil.rmtree(project)
    shutil.copytree(os.path.join(ROOT, "Samples", sample), project)
    return project


def sample_script(project):
    lines = ["wait 40"]
    for scene in sorted(f[:-8] for f in os.listdir(os.path.join(project, "Assets", "Scenes")) if f.endswith(".cqscene")):
        lines += [f"open_scene Assets/Scenes/{scene}.cqscene", "wait 20", "focus Game", "script_errors mark", "play", "wait 90",
                  "script_errors expect 0", "stop", "wait 10"]
    return "\n".join(lines + ["quit"]) + "\n"


def make_project(tid, name, env=None):
    os.makedirs(WORK, exist_ok=True)
    project = os.path.join(WORK, name)
    if os.path.isdir(project):
        shutil.rmtree(project)
    script = os.path.join(WORK, f"_hub_{name}.txt")
    with open(script, "w") as f:
        f.write(f'wait 20\nhub create {tid} {name} "{WORK}"\nwait 5\nquit\n')
    # Testlerin Hub'ı kendi ayar klasörünü kullanır: kullanıcının proje listesine (hub.yaml) test projeleri eklenmez
    env = dict(env or os.environ)
    if "CQ_HUB_CONFIG_DIR" not in env:
        cfg = os.path.join(WORK, "_hubconfig")
        os.makedirs(cfg, exist_ok=True)
        if not os.path.exists(os.path.join(cfg, "hub.yaml")):
            with open(os.path.join(cfg, "hub.yaml"), "w") as f:
                f.write(f'Settings:\n  DefaultLocation: "{WORK}"\nProjects: []\n')
        env["CQ_HUB_CONFIG_DIR"] = cfg
    runner = os.environ.get("CQ_HUB_RUNNER")
    if runner:
        run(["bash", runner, script], env=env)
    else:
        run([os.path.join(BIN, "ConquerorLauncher")], env=dict(env or os.environ, CQ_AUTOMATION=script, CQ_AUDIO_NULL="1"), cwd=BIN)
    return project if os.path.isdir(project) else None


def run_editor(script, project, env=None):
    if os.path.exists(script + ".result.txt"):
        os.remove(script + ".result.txt")
    runner = os.environ.get("CQ_RUNNER")
    if runner:
        code, out = run(["bash", runner, script, project], env=env)
        m = re.search(r"ENGINE_EXIT=(\d+)", out)
        code = int(m.group(1)) if m else code
    else:
        code, out = run([os.path.join(BIN, "ConquerorEngine"), project], env=dict(env or os.environ, CQ_AUTOMATION=script, CQ_AUDIO_NULL="1"), cwd=BIN)
    result = open(script + ".result.txt").read() if os.path.exists(script + ".result.txt") else ""
    return code, result


def run_hub(script, name, env=None):
    # Hub testleri: her test kendi boş ayar klasörüyle (kullanıcının ve diğer testlerin listesine dokunmaz)
    if os.path.exists(script + ".result.txt"):
        os.remove(script + ".result.txt")
    cfg = os.path.join(WORK, f"_hubcfg_{name}")
    if os.path.isdir(cfg) and os.path.basename(cfg).startswith("_hubcfg_"):
        shutil.rmtree(cfg)
    os.makedirs(cfg)
    with open(os.path.join(cfg, "hub.yaml"), "w") as f:
        f.write(f'Settings:\n  DefaultLocation: "{WORK}"\nProjects: []\n')
    env = dict(env or os.environ, CQ_HUB_CONFIG_DIR=cfg)
    runner = os.environ.get("CQ_HUB_RUNNER")
    if runner:
        code, out = run(["bash", runner, script], env=env)
        m = re.search(r"HUB_EXIT=(\d+)", out)
        code = int(m.group(1)) if m else code
    else:
        code, out = run([os.path.join(BIN, "ConquerorLauncher")], env=dict(env, CQ_AUTOMATION=script, CQ_AUDIO_NULL="1"), cwd=BIN)
    result = open(script + ".result.txt").read() if os.path.exists(script + ".result.txt") else ""
    return code, result


def case_script(name):
    text = open(os.path.join(CASES, name + ".txt"), encoding="utf-8").read()
    m = re.search(r"^#\s*template:\s*(\S+)", text, re.M)
    return (m.group(1) if m else "empty"), text.replace("{dir}", CASES).replace("{work}", WORK).replace("{bin}", BIN)


def template_script():
    return "\n".join(["wait 30", "focus Game", "script_errors mark", "play", "wait 240", "script_errors expect 0", "stop", "quit"]) + "\n"


def matrix_of(name):
    # "# matrix: VAR=v1 v2 ..." -> (VAR, [v1, v2, ...]) ya da None
    path = os.path.join(CASES, name + ".txt")
    if not os.path.exists(path):
        return None
    m = re.search(r"^#\s*matrix:\s*([A-Za-z_][A-Za-z0-9_]*)=(.+)$", open(path, encoding="utf-8").read(), re.M)
    return (m.group(1), m.group(2).split()) if m else None


def is_serial(case):
    if case.startswith(("template:", "sample:")):
        return False
    path = os.path.join(CASES, case.split("@", 1)[0] + ".txt")
    return os.path.exists(path) and re.search(r"^#\s*serial\b", open(path, encoding="utf-8").read(), re.M) is not None


def run_case(case, env=None):
    result = run_case_inner(case, env)
    if not case.startswith(("template:", "sample:")):
        _, text = case_script(case.partition("@")[0])
        for cmd in re.findall(r"^#\s*teardown:\s*(.+)$", text, re.M):
            run(shlex.split(cmd), env=env)
    return result


def run_case_inner(case, env=None):
    start = time.time()
    if case.startswith("sample:"):
        name = "Reg_" + re.sub(r"[^A-Za-z0-9_]", "_", case)
        project = sample_project(case.split(":", 1)[1], name)
        script = os.path.join(WORK, f"_{name}.txt")
        with open(script, "w", encoding="utf-8") as f:
            f.write(sample_script(project))
        code, result = run_editor(script, project, env)
        fails = [l for l in result.splitlines() if " FAIL " in l or l.startswith("FAIL")]
        done = re.search(r"DONE failures=(\d+)", result)
        if not done:
            return False, "automation did not finish (crash or timeout)", time.time() - start
        if int(done.group(1)) != 0:
            return False, "; ".join(fails)[:300] or f"{done.group(1)} failures", time.time() - start
        if code != 0:
            return False, f"editor exit code {code} (crash at shutdown?)", time.time() - start
        return True, "", time.time() - start
    if case.startswith("template:"):
        tid = case.split(":", 1)[1]
        text = template_script()
    else:
        base, _, value = case.partition("@")
        tid, text = case_script(base)
        matrix = matrix_of(base)
        if matrix and value:
            text = text.replace("{matrix}", value)
            env = dict(env or os.environ, **{matrix[0]: value})
        # Donanım / çalışma zamanı isteyen testler (ör. OpenXR: Monado) yalnız o ortamda; ek ortam değişkenleri
        for var in re.findall(r"^#\s*requires:\s*(\S+)", text, re.M):
            if not os.environ.get(var):
                return None, f"{var} not set", time.time() - start
        extra = dict(kv.split("=", 1) for line in re.findall(r"^#\s*env:\s*(.+)$", text, re.M) for kv in line.split() if "=" in kv)
        if extra:
            env = dict(env or os.environ, **extra)
    name = "Reg_" + re.sub(r"[^A-Za-z0-9_]", "_", case)
    if not case.startswith("template:"):
        # Hazırlık komutları (depodaki betikler, ör. sahte sürüm düzeneği)
        for cmd in re.findall(r"^#\s*setup:\s*(.+)$", text, re.M):
            code, out = run(shlex.split(cmd), env=env)
            if code != 0:
                return False, f"setup failed: {cmd}: {out.strip()[-200:]}", time.time() - start
        if re.search(r"^#\s*hub\b", text, re.M):
            script = os.path.join(WORK, f"_{name}.txt")
            with open(script, "w", encoding="utf-8") as f:
                f.write(text)
            code, result = run_hub(script, name, env)
            fails = [l for l in result.splitlines() if " FAIL " in l or l.startswith("FAIL")]
            done = re.search(r"DONE failures=(\d+)", result)
            if not done:
                return False, "hub automation did not finish (crash or timeout)", time.time() - start
            if int(done.group(1)) != 0:
                return False, "; ".join(fails)[:300] or f"{done.group(1)} failures", time.time() - start
            if code != 0:
                return False, f"hub exit code {code}", time.time() - start
            return True, "", time.time() - start
    project = make_project(tid, name, env)
    if not project:
        return False, f"hub could not create a '{tid}' project", time.time() - start
    script = os.path.join(WORK, f"_{name}.txt")
    with open(script, "w", encoding="utf-8") as f:
        f.write(text)
    code, result = run_editor(script, project, env)
    fails = [l for l in result.splitlines() if " FAIL " in l or l.startswith("FAIL")]
    done = re.search(r"DONE failures=(\d+)", result)
    if not done:
        return False, "automation did not finish (crash or timeout)", time.time() - start
    if int(done.group(1)) != 0:
        return False, "; ".join(fails)[:300] or f"{done.group(1)} failures", time.time() - start
    if code != 0:
        return False, f"editor exit code {code} (crash at shutdown?)", time.time() - start
    for cmd in re.findall(r"^#\s*check:\s*(.+)$", text, re.M):
        ccode, cout = run(shlex.split(cmd), env=env)
        if ccode != 0:
            return False, f"check failed: {cout.strip()[-300:]}", time.time() - start
    return True, "", time.time() - start


def all_cases():
    named = []
    for name in sorted(f[:-4] for f in os.listdir(CASES) if f.endswith(".txt")):
        matrix = matrix_of(name)
        named += [f"{name}@{v}" for v in matrix[1]] if matrix else [name]
    return [f"template:{t}" for t in hub_templates()] + [f"sample:{s}" for s in samples()] + named


def main():
    args = sys.argv[1:]
    if "--list" in args:
        print("\n".join(all_cases()))
        return
    jobs = 1
    if "-j" in args:
        i = args.index("-j")
        jobs = max(1, int(args[i + 1]))
        del args[i:i + 2]
    # Matrisli testin adı tek başına verilirse her değeri çalışır (gl_profiles -> gl_profiles@4.6 ...)
    cases = []
    for c in args or all_cases():
        matrix = None if "@" in c or c.startswith(("template:", "sample:")) else matrix_of(c)
        cases += [f"{c}@{v}" for v in matrix[1]] if matrix else [c]
    results = []
    lock = threading.Lock()

    def report(c, ok, why, secs):
        with lock:
            results.append((c, ok, why))
            print(f"{'SKIP' if ok is None else 'PASS' if ok else 'FAIL'}  {c:34s} {secs:6.1f} s  {why}", flush=True)

    if jobs == 1:
        for c in cases:
            report(c, *run_case(c))
    else:
        # Her işçi kendi oyun kayıt klasörüyle (XDG_DATA_HOME); zamana duyarlı (# serial) testler sonda tek başına
        base = os.environ.get("XDG_DATA_HOME") or os.path.join(WORK, "_xdg")
        slots = list(range(jobs))
        slot_lock = threading.Lock()

        def worker(c):
            with slot_lock:
                slot = slots.pop()
            try:
                env = dict(os.environ, XDG_DATA_HOME=os.path.join(base, f"slot{slot}"))
                os.makedirs(env["XDG_DATA_HOME"], exist_ok=True)
                return run_case(c, env)
            finally:
                with slot_lock:
                    slots.append(slot)

        parallel = [c for c in cases if not is_serial(c)]
        serial = [c for c in cases if is_serial(c)]
        with ThreadPoolExecutor(max_workers=jobs) as ex:
            futures = {ex.submit(worker, c): c for c in parallel}
            for f in as_completed(futures):
                report(futures[f], *f.result())
        for c in serial:
            report(c, *run_case(c))
    failed = [r for r in results if r[1] is False]
    skipped = [r for r in results if r[1] is None]
    print(f"\n{len(results) - len(failed) - len(skipped)} / {len(results) - len(skipped)} passed" + (f", {len(skipped)} skipped" if skipped else ""))
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
