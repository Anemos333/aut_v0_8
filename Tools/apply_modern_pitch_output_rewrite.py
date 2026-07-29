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

# Workflow files are intentionally left untouched here: GitHub Actions may not
# update workflow definitions with its ordinary repository token. They are
# cleaned up separately through the GitHub connector after this commit lands.
for relative in [
    "Tests/OutputV3ShadowLedgerIdentity.cpp",
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
