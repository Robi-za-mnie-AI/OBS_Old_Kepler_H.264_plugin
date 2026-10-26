# nvenc old kepler

Windows-only OBS Studio plugin for H.264 NVENC using NVIDIA NVENC API 8.1, intended for legacy NVIDIA Kepler GPUs and older drivers.

## Build

The project is built on GitHub Actions for Windows x64 only. Run **Build NVENC Old Kepler (Windows x64)** from the Actions tab, or push a commit. The workflow uploads a ZIP artifact.

The build preset enables `LEGACY_NVENC_API` and disables HEVC. The plugin only targets H.264.

## Included files

Only Windows build configuration, shared CMake support, source files used by the Windows target, and English/Polish locale resources are retained. macOS/Linux build files, unrelated tests, and development notes have been removed.
