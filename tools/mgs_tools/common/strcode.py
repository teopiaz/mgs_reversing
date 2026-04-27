"""GV_StrCode — the engine-wide 16-bit name hash.

The runtime implementation lives in source/libgv/strcode.c. Every asset
id (KMD / HZD / PCX / GCL proc / chara symbol / ...) is the strcode of
its source-level name, so almost every Python tool ends up needing this.

Single source of truth for the package; both `mgs_tools.gcl.constants`
and `mgs_tools.stage.constants` re-export from here for back-compat.
"""


def gv_strcode(s: str) -> int:
    """Replicates source/libgv/strcode.c GV_StrCode(): 5-bit rol + add."""
    h = 0
    for b in s.encode("utf-8"):
        h = ((h << 5) | (h >> 11)) & 0xFFFF
        h = (h + b) & 0xFFFF
    return h
