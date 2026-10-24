# OptiScaler source, license and notices

This aggregate contains an unmodified OptiScaler ASI-loading binary. The editor remains version 0.2.1; OptiScaler is a separate work by its upstream authors under GPL-3.0. The included loader INI is a local packaging configuration, not a modified binary. Source access is available without charge.

- [Official binary release](https://github.com/optiscaler/OptiScaler-nightly/releases/tag/nightly-20261019)
- [Exact main-source archive](https://github.com/optiscaler/OptiScaler/archive/1db5042aa57425d59ac979b20d43eea0053a15a5.zip)
- [Exact source commit](https://github.com/optiscaler/OptiScaler/tree/1db5042aa57425d59ac979b20d43eea0053a15a5)
- [GPL-3.0 license](licenses/LICENSE-OptiScaler-GPL-3.0.txt)
- [Dependency commits and archives](Dependency-source.json)

The main-source archive does not embed Git submodules. Obtain the matching dependency sources below, or use Git to check out the exact main commit and recursively initialize its recorded submodules. Build instructions and project files are in that source tree. Keep these source directions next to the loader download and preserve the loader's license rights separately from the editor's reuse permissions.

```text
git clone --no-checkout https://github.com/optiscaler/OptiScaler.git OptiScaler-source
git -C OptiScaler-source checkout 1db5042aa57425d59ac979b20d43eea0053a15a5
git -C OptiScaler-source submodule update --init --recursive
```

## Matching dependency sources

- [external/simpleini](https://github.com/brofield/simpleini/archive/6048871ea9ee0ec24be5bd099d161a10567d7dc2.zip) — commit 6048871ea9ee0ec24be5bd099d161a10567d7dc2
- [external/unordered_dense](https://github.com/martinus/unordered_dense/archive/73f3cbb237e84d483afafc743f1f14ec53e12314.zip) — commit 73f3cbb237e84d483afafc743f1f14ec53e12314
- [external/xess](https://github.com/intel/xess/archive/8fe81bdbbaf00b3c1b733fd0d830c333dc84e6f0.zip) — commit 8fe81bdbbaf00b3c1b733fd0d830c333dc84e6f0
- [external/vulkan](https://github.com/KhronosGroup/Vulkan-Headers/archive/d64e9e156ac818c19b722ca142230b68e3daafe3.zip) — commit d64e9e156ac818c19b722ca142230b68e3daafe3
- [external/spdlog](https://github.com/gabime/spdlog/archive/faa0a7a9c5a3550ed5461fab7d8e31c37fd1a2ef.zip) — commit faa0a7a9c5a3550ed5461fab7d8e31c37fd1a2ef
- [external/FidelityFX-SDK](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/archive/c6efa6bf7f2027b3ec94f28578bb5965eabb9e55.zip) — commit c6efa6bf7f2027b3ec94f28578bb5965eabb9e55
- [external/magic_enum](https://github.com/Neargye/magic_enum/archive/a733a2ea665ca5d72b7270f0334bf2e7b82bd0cc.zip) — commit a733a2ea665ca5d72b7270f0334bf2e7b82bd0cc
- [external/FidelityFX-SDK-v2](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/archive/60f4ea81909200d8542eca14dccb2628b763a9a3.zip) — commit 60f4ea81909200d8542eca14dccb2628b763a9a3
- [external/nvapi](https://github.com/NVIDIA/nvapi/archive/9b181ea572f680327fe01a14a0f1f41c78034104.zip) — commit 9b181ea572f680327fe01a14a0f1f41c78034104

Additional upstream notices are retained in licenses/. No original game signature, personal settings, game EXE, graphics frame-generation mod or machine log is included.
