MuJoCo 3.17.0 C API headers (Apache-2.0), vendored for dynamic loading.
The runtime library (libmujoco) is NOT vendored: Conqueror loads it at runtime when the
MuJoCo physics backend is selected (Project Settings > Physics). Search order:
  $MUJOCO_DIR/lib, <exe>/mujoco/lib, /opt/mujoco*/lib, ~/.mujoco/*/lib, system paths.
