# Tiny helpers for grep-lock tests. No runtime behavior.
from pathlib import Path

HERE = Path(__file__).resolve().parent
KEPLER = HERE.parents[1]


def read_src(*parts: str) -> str:
    return KEPLER.joinpath(*parts).read_text(encoding="utf-8")
