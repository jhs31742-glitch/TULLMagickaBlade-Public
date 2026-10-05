# Third-Party Notices

This document records the principal third-party software used by the
`[TULL] Magicka Blade` native plugin source/build.

The project source archive does not vendor the complete upstream repositories.
When third-party source or binaries are redistributed, their upstream license
files and notices must be retained as required by their respective licenses.

---

## CommonLibSSE-NG

- Project: CommonLibSSE-NG
- Version used: v4.18.0
- Commit: `8c4025b01fac2bea1bbe73a3a9da7b4fde338343`
- Repository used by the project submodule:
  `https://github.com/alandtse/CommonLibVR.git` (`ng` branch)
- License at the pinned commit: MIT
- Copyright: Copyright (c) 2018 Ryan-rsm-McKenzie

CommonLibSSE-NG provides the SKSE/CommonLib interface used by this plugin.

---

## spdlog

- Project: spdlog
- Repository: `https://github.com/gabime/spdlog`
- License: MIT
- Copyright: Copyright (c) 2016 - present, Gabi Melman and spdlog contributors

spdlog is used for plugin logging.

spdlog may use the `{fmt}` library as a dependency.

---

## {fmt}

- Project: fmt
- Repository: `https://github.com/fmtlib/fmt`
- License: MIT
- Copyright: Copyright (c) 2012 - present, Victor Zverovich and {fmt} contributors

`{fmt}` may be pulled transitively by spdlog / the dependency graph.

---

## DirectXTK

- Project: DirectX Tool Kit for DirectX 11
- Repository: `https://github.com/microsoft/DirectXTK`
- License: MIT
- Copyright: Copyright (c) Microsoft Corporation

DirectXTK is declared in the vcpkg dependency manifest and is resolved before
CommonLibSSE-NG is added by CMake.

---

## rapidcsv

- Project: rapidcsv
- Repository: `https://github.com/d99kris/rapidcsv`
- License: BSD-3-Clause
- Copyright: Copyright (c) 2017-2026 Kristofer Berggren

rapidcsv is declared in the vcpkg dependency manifest because the pinned
CommonLibSSE-NG build exposes it through its dependency setup.

---

# MIT License Text

The following license text applies to third-party components above that are
identified as MIT-licensed. Their individual copyright notices are listed in
their respective sections.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

---

# BSD 3-Clause License Text

The following license applies to rapidcsv.

Copyright (c) 2017-2026, Kristofer Berggren
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors
   may be used to endorse or promote products derived from this software
   without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

---

## Build tooling

vcpkg and other development tools have their own upstream licenses. They are
used to obtain/build dependencies and are not part of the original
`[TULL] Magicka Blade` source license.

This notice is intended to accompany the source release. If the dependency
set changes in a future release, update this file together with the lock /
baseline information.
