#!/usr/bin/env python3
"""build_portable_package testi (264): derlenen .cqspace paketini oyuncuyla (ConquerorRuntime) başsız açar ve oyuncunun otomasyon
komutlarıyla denetler: paket çözülür, oyunun başlangıç sahnesi yüklenir; daha yeni motor isteyen paket açık bir mesajla reddedilir.

usage: run_player.py <bin klasörü> <paketin klasörü>
"""
import glob
import os
import subprocess
import sys
import tempfile

bin_dir, out_dir = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
packages = glob.glob(os.path.join(out_dir, "*.cqspace"))
if not packages:
    sys.exit(f"no .cqspace in {out_dir}")
package = packages[0]
work = tempfile.mkdtemp(prefix="cqspace_test_")
script = os.path.join(work, "player.txt")
with open(script, "w") as f:
    f.write("wait_until 60000 game mode project\nwait 30\ngame report\ngame expect_refuses_engine 9.999.0 newer\nquit\n")
env = dict(os.environ, CQ_AUTOMATION=script, CQ_AUDIO_NULL="1", XDG_CACHE_HOME=os.path.join(work, "cache"))
name = f"cqplayer-{os.getpid()}"
cmd = ["timeout", "200", "dbus-run-session", "--", "mutter", "--headless", "--virtual-monitor", "1280x720", "--wayland-display", name, "--",
       "sh", "-c", f'timeout 160 "{bin_dir}/ConquerorRuntime" "{package}"; echo PLAYER_EXIT=$?']
p = subprocess.run(cmd, cwd=bin_dir, env=env, capture_output=True, text=True)
result = open(script + ".result.txt").read() if os.path.exists(script + ".result.txt") else ""
print("\n".join(l for l in result.splitlines() if "GAME " in l or "FAIL" in l or "DONE" in l))
if "DONE failures=0" not in result:
    print((p.stdout + p.stderr)[-1500:])
    sys.exit(1)
print("player opened", os.path.basename(package))
