from pathlib import Path
import base64
import re
import shutil
import zlib

root = Path.cwd()
payload_root = root / "Tools" / "rewrite_payload"


def decode(parts: list[str], destination: str) -> None:
    encoded = "".join((payload_root / part).read_text() for part in parts)
    data = zlib.decompress(base64.b64decode(encoded))
    path = root / destination
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


decode(["header.00", "header.01", "header.02", "header.03"],
       "Source/ModernPitchEngine.h")
decode(["output.00", "output.01", "output.02", "output.03", "output.04"],
       "Source/ModernPitchOutputStage.cpp")

engine_path = root / "Source" / "ModernPitchEngine.cpp"
engine = engine_path.read_text()
engine = re.sub(
    r"\n// V3 ridge tracking remains available independently for shadow validation\..*?#endif\n\nnamespace\n\{",
    "\nnamespace\n{",
    engine,
    count=1,
    flags=re.S,
)
start = engine.index("// SpectralVoiceShifter")
end = engine.index("// FixedDelay", start)
engine_path.write_text(engine[:start] + engine[end:])

cmake_path = root / "CMakeLists.txt"
cmake = cmake_path.read_text()
cmake = re.sub(
    r"\n# -----------------------------------------------------------------------------\n# V3 Stage B validation targets\..*?\nendif\(\)\n\n",
    "\n",
    cmake,
    count=1,
    flags=re.S,
)
cmake = cmake.replace(
    "        Source/ModernPitchEngine.cpp\n",
    "        Source/ModernPitchEngine.cpp\n        Source/ModernPitchOutputStage.cpp\n",
)
cmake = re.sub(
    r"\n    PRIVATE\n        NEUMATON_OUTPUT_V3_SHADOW_LEDGER=1\n        NEUMATON_OUTPUT_V3_SHADOW_RENDERER=1\n        NEUMATON_OUTPUT_V3_AUDIO_RENDERER=1",
    "",
    cmake,
    count=1,
)
cmake = re.sub(
    r"\n            NEUMATON_OUTPUT_V3_SHADOW_LEDGER=1\n            NEUMATON_OUTPUT_V3_SHADOW_RENDERER=1\n            NEUMATON_OUTPUT_V3_AUDIO_RENDERER=1",
    "",
    cmake,
    count=1,
)
cmake_path.write_text(cmake)

(root / ".github" / "workflows" / "output-v3-shadow-ledger.yml").write_text(
"""name: Ridge Ledger Lifecycle

on:
  pull_request:
    paths:
      - 'Source/NeumatonOutputTypes.h'
      - 'Source/NeumatonRidgeLedger.*'
      - 'Tests/OutputV3ShadowLedgerLifecycle.cpp'
      - '.github/workflows/output-v3-shadow-ledger.yml'

permissions:
  contents: read

jobs:
  lifecycle-unix:
    runs-on: ubuntu-24.04
    strategy:
      matrix:
        compiler: [g++, clang++]
    steps:
      - uses: actions/checkout@v4
      - name: Compile and run lifecycle test
        run: |
          ${{ matrix.compiler }} -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror \
            -ISource Source/NeumatonRidgeLedger.cpp \
            Tests/OutputV3ShadowLedgerLifecycle.cpp -o ridge_ledger_lifecycle
          ./ridge_ledger_lifecycle

  lifecycle-windows:
    runs-on: windows-2025
    steps:
      - uses: actions/checkout@v4
      - uses: ilammy/msvc-dev-cmd@v1
      - name: Compile and run lifecycle test
        shell: cmd
        run: |
          cl /nologo /std:c++17 /O2 /EHsc /W4 /WX /ISource Source\\NeumatonRidgeLedger.cpp Tests\\OutputV3ShadowLedgerLifecycle.cpp /Fe:ridge_ledger_lifecycle.exe
          ridge_ledger_lifecycle.exe
""")

for relative in [
    "Tests/OutputV3ShadowLedgerIdentity.cpp",
    ".github/workflows/export-modern-engine-source.yml",
    ".github/workflows/rewrite-modern-pitch-output.yml",
    "Tools/apply_modern_pitch_output_rewrite.py",
]:
    path = root / relative
    if path.exists():
        path.unlink()

if payload_root.exists():
    shutil.rmtree(payload_root)

for relative in [
    "Source/ModernPitchEngine.cpp",
    "Source/ModernPitchOutputStage.cpp",
    "Source/ModernPitchEngine.h",
]:
    path = root / relative
    print(relative, sum(1 for _ in path.open()))
