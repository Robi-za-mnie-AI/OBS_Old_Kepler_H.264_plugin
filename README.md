# nvenc old kepler

Windows-only OBS Studio plugin for H.264 NVENC using NVIDIA NVENC API 8.1, intended for legacy NVIDIA Kepler GPUs and older drivers.
Based on https://github.com/RanAwaySuccessfully/nvenckepler and modified using AI.

Install to "C:\ProgramData\obs-studio\plugins" or to the ".\plugins" folder in the portable version.

## Test info
Tested on Quadro K2100M. 1920x1080, 30 FPS - ~40% GPU load

## Theoretical compatibility list
Theoretically, the plugin should also support:

GTX 650
GTX 650 Ti, GTX 650 Ti Boost
GTX 660, GTX 660 Ti
GTX 670, GTX 680
GTX 690
GTX 760, GTX 760 Ti
GTX 770
GTX 780, GTX 780 Ti
GTX Titan, GTX Titan Black, GTX Titan Z

GTX 650M, GTX 660M
GTX 745M, GTX 750M, GTX 755M, GTX 760M, GTX 765M, GTX 770M, GTX 780M
GTX 860M, GTX 870M, GTX 880M

Quadro - 410, K420, K600
Quadro - K2000, K2000D, K4000
Quadro - K4200, K5000, K5200, K6000
Quadro - NVS 510
Quadro Mobile	- K500M, K510M, K610M
Quadro Mobile -	K1000M, K1100M, K2000M, K2100M
Quadro Mobile - K3000M, K3100M, K4000M, K4100M, K5000M, K5100M


With the caveat that it might not work on every card model (as there were different cores)

GeForce GT 640
GeForce GT 710
GeForce GT 720
GeForce GT 730
GeForce GT 740
