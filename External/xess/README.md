# Intel XeSS SDK headers

`include/xess/xess.h` and `include/xess/xess_vk.h` from the Intel XeSS SDK (https://github.com/intel/xess), MIT license (`LICENSE-headers.txt`).

The engine loads the XeSS runtime (`libxess.dll`, Windows only) at run time; the DLL is not in this repository. It may be redistributed
next to a game under the Intel Simplified Software License (`LICENSE-binaries.txt`): `Tools/XeSS/fetch-xess.sh`
downloads it, Build Settings copies it next to Windows games when it is present beside the editor.
