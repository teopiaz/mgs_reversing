"""Compile a GclNode AST back into .gcx bytecode.

Inverse of gcl_decompile.py. Derived from mgs_compilation_tools
(see README.md for attribution).

The resulting .gcx can be loaded by the runtime in
[source/libgcl/command.c:150](../../source/libgcl/command.c#L150).
"""
import sys

from .constants import GclCode, GclCommand, GclOperator
from .gcx import GcxData, GclNode


class GclComp:
    """AST → bytecode."""

    def __init__(self, align4: bool = True):
        """align4: PSX builds pad the trailing stream to a 4-byte boundary."""
        self.gcx = GcxData()
        self.align4 = align4

    def compile_file(self, tree) -> GcxData:
        """Compile the top-level node list (proc defs + main script)."""
        header = GcxData()          # proc table entries
        proc_bodies = GcxData()     # concatenated proc bodies
        main_body = GcxData()
        fonts_data = GcxData()

        for elem in tree:
            if "FONTS" in elem:
                for font in elem["FONTS"]:
                    fonts_data.extend(bytes.fromhex(font))
                continue

            proc_id = elem["PROC_ID"]
            body = self._compile(elem["PROC_DATA"])

            if proc_id is None:
                # Main script body. The runtime (command.c:161) reads its
                # length from the 4-byte header that precedes it.
                main_body = body
            else:
                header.push_short(proc_id)
                header.push_short(len(proc_bodies))
                proc_bodies.extend(body)

        data = GcxData()
        # Proc region: 4-byte length, then table + terminator + bodies.
        proc_region = GcxData()
        proc_region.extend(header)
        proc_region.push_int(0)      # table terminator
        proc_region.extend(proc_bodies)
        data.push_int(len(proc_region))
        data.extend(proc_region)

        # Script region: 4-byte length, then main body.
        data.push_int(len(main_body))
        data.extend(main_body)

        # Trailing font/extra blob.
        data.extend(fonts_data)

        if self.align4:
            while len(data) % 4 != 0:
                data.push_byte(0)

        self.gcx = data
        return data

    # ------------------------------------------------------------ node

    def _compile(self, node) -> GcxData:
        out = GcxData()

        if isinstance(node, list):
            for child in node:
                out.extend(self._compile(child))
            return out

        code, value = node.get_pair()

        match code:
            case GclCode.WORD.name:
                v = int(value)
                if v < 0:
                    v = v + 0x10000
                out.push_short(v & 0xFFFF)

            case GclCode.BYTE.name:
                out.push_byte(int(value) & 0xFF)

            case GclCode.CHAR.name:
                out.push_byte(ord(value))

            case GclCode.FLAG.name:
                out.push_byte(1 if value else 0)

            case GclCode.STR_ID.name:
                out.push_short(int(value) & 0xFFFF)

            case GclCode.STR.name:
                enc = _encode_string(value)
                out.push_byte(len(enc))
                out.extend(enc)

            case GclCode.PROC.name:
                out.push_short(int(value) & 0xFFFF)

            case GclCode.SD_CODE.name:
                out.push_int(int(value) & 0xFFFFFFFF)

            case GclCode.TABLE.name:
                if isinstance(value, str):
                    raise ValueError(
                        f"TABLE value must already be an int; got str {value!r}. "
                        "Name resolution (radio/vox/demo) is out of scope."
                    )
                out.push_int(int(value) & 0xFFFFFFFF)

            case GclCode.VAR.name:
                inner_t, inner_v = value.get_pair()
                out.push_byte(GclCode.VAR.value + GclCode[inner_t].value)
                out.extend(bytes.fromhex(inner_v))
                return out  # already includes opcode

            case GclCode.ARG.name:
                out.push_byte(int(value) & 0xFF)

            case GclCode.EXPR.name:
                body = GcxData()
                for operand in value:
                    body.extend(self._compile(operand))
                body.push_byte(GclCode.OP.value)
                body.push_byte(0)       # OP_NULL terminator
                out.push_byte(len(body) + 1)
                out.extend(body)

            case GclCode.OP.name:
                op_name, operands = value.get_pair()
                out.extend(self._compile(operands[0]))
                out.extend(self._compile(operands[1]))
                out.push_byte(GclCode.OP.value)
                out.push_byte(GclOperator[op_name].value)
                return out

            case GclCode.SCRIPT.name:
                body = self._compile(value)
                body.push_byte(0)       # NULL terminator
                out.push_short(len(body) + 2)
                out.extend(body)
                if node.get("NO_BRACES"):
                    out = out[:-1]

            case GclCode.OPTION.name:
                letter, values = value.get_pair()
                payload = self._compile(values)
                out.push_byte(ord(letter))
                if value.get("NULL_SIZE"):
                    out.push_byte(0)
                else:
                    out.push_byte(len(payload) + 1)
                out.extend(payload)

            case GclCode.CMD.name:
                cmd_name, items = value.get_pair()
                cmd_id = GclCommand[cmd_name].value if cmd_name in GclCommand.__members__ else int(cmd_name[4:], 16)

                cmd = GcxData()
                cmd.push_short(cmd_id)

                # Positional args come first, then options.
                pos_args = GcxData()
                opts = []
                for it in items:
                    t, _ = it.get_pair()
                    if t == GclCode.OPTION.name:
                        opts.append(it)
                    else:
                        pos_args.extend(self._compile(it))

                args_size = len(pos_args) + 1
                if cmd_name == GclCommand.IF.name and len(items) == 2:
                    # `if` with body and no else/elseif: runtime adds 1.
                    args_size += 1

                cmd.push_byte(args_size)
                cmd.extend(pos_args)

                # Compile options in order; patch elseif/else size quirks.
                opt_bytes = GcxData()
                for i, opt in enumerate(items):
                    t, v = opt.get_pair()
                    if t != GclCode.OPTION.name:
                        continue
                    opt_data = self._compile(opt)
                    if cmd_name == GclCommand.IF.name:
                        letter, payload = v.get_pair()
                        is_last = (i == len(items) - 1)
                        if letter == "i" and len(payload) == 2 and is_last:
                            opt_data[2] += 1
                        elif letter == "e" and len(payload) == 1 and is_last:
                            opt_data[2] += 1
                    opt_bytes.extend(opt_data)

                cmd.extend(opt_bytes)
                cmd.push_byte(0)        # NULL terminator

                out.push_short(len(cmd) + 2)
                out.extend(cmd)

            case GclCode.CALL.name:
                proc_id, proc_args = value.get_pair()
                call = GcxData()
                call.push_short(int(proc_id))
                for arg in proc_args:
                    call.extend(self._compile(arg))
                call.push_byte(0)
                out.push_byte(len(call) + 1)
                out.extend(call)

            case _:
                raise ValueError(f"unexpected node type: {code}")

        out.insert(0, GclCode[code].value)
        return out


def _encode_string(value: str) -> bytes:
    """Inverse of gcx.GcxData.read_string().

    - `\\xNN` escapes                          → single raw byte
    - `{XXXX}` markers                         → raw 2-byte code (big-endian)
    - Chars in mgs_chars.CHAR_TO_CODE table    → raw 2-byte code
    - ASCII chars (< 0x80)                     → single raw byte
    """
    from .glyphs.chars import CHAR_TO_CODE

    out = bytearray()
    i = 0
    while i < len(value):
        c = value[i]
        # \xNN hex escape
        if c == "\\" and i + 3 < len(value) and value[i + 1] == "x":
            out.append(int(value[i + 2:i + 4], 16))
            i += 4
            continue
        # {XXXX} 2-byte code marker
        if c == "{" and i + 5 < len(value) and value[i + 5] == "}":
            code = int(value[i + 1:i + 5], 16)
            out.append((code >> 8) & 0xFF)
            out.append(code & 0xFF)
            i += 6
            continue
        # Known glyph → 2-byte code via table
        if c in CHAR_TO_CODE:
            code = CHAR_TO_CODE[c]
            out.append((code >> 8) & 0xFF)
            out.append(code & 0xFF)
            i += 1
            continue
        # ASCII (< 0x80)
        cp = ord(c)
        if cp < 0x80:
            out.append(cp)
            i += 1
            continue
        raise ValueError(
            f"char {c!r} (U+{cp:04X}) has no MGS code; add it to "
            "mgs_chars.CODE_TO_CHAR or escape the bytes as \\xNN / {XXXX}"
        )
    out.append(0)
    return bytes(out)
