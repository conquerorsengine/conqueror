#!/usr/bin/env python3
"""build_prebuilt_player testi (47): editörün klasörüne Hub'dan kurulmuş gibi bir "Windows Build Support" modülü koyar (içindeki
oyuncu, bu bilgisayarın oyuncusunun .exe adıyla bir kopyası: yalnız paketleme denenir, çalıştırılmaz) ya da kaldırır.

usage: fake_player_module.py <editör bin klasörü> create|remove
"""
import os
import shutil
import sys

bin_dir, action = os.path.abspath(sys.argv[1]), sys.argv[2]
module = os.path.join(bin_dir, "Modules", "windows")
marker = os.path.join(module, "FAKE_TEST_MODULE")
if action == "remove":
    if os.path.isfile(marker):
        shutil.rmtree(module)
    modules = os.path.join(bin_dir, "Modules")
    if os.path.isdir(modules) and not os.listdir(modules):
        os.rmdir(modules)
else:
    if os.path.isdir(module) and not os.path.isfile(marker):
        sys.exit("a real windows module is installed here; not touching it")
    player = os.path.join(module, "player", "windows", "bin")
    os.makedirs(player, exist_ok=True)
    shutil.copy2(os.path.join(bin_dir, "ConquerorRuntime"), os.path.join(player, "ConquerorRuntime.exe"))
    open(os.path.join(player, "fake.dll"), "wb").write(b"MZ fake")
    open(marker, "w").write("created by the regression test build_prebuilt_player\n")
print("ok", action, module)
