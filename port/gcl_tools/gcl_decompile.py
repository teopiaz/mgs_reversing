"""Decompile a .gcx binary to a GclNode AST.

The bytecode format matches the runtime parser in source/libgcl/parse.c,
command.c, variable.c, and expr.c. All multi-byte integers are big-endian.

File layout:
    [proc_region_len:4]
    [ {proc_id:2, offset:2}, ... 0x00000000 terminator ]  -- proc table
    [ proc bodies ... ]                                   -- each starts with 0x40 SCRIPT
    [script_region_len:4]
    [ 0x40 SCRIPT main body ]
    [ trailing font/data ]
"""
import sys

from constants import GclCode, GclCommand, GclOperator
from gcx import GcxData, GclNode


class GclDecomp:
    """Walk .gcx bytes, emit a GclNode tree."""

    def __init__(self, gcx: GcxData):
        self.gcx = gcx
        self.procedures = []
        self.tree_data = []
        self.commands_stack = []
        self.fonts_size = 0

    # ------------------------------------------------------------------ entry

    def decompile_gcx_file(self):
        # 1. Proc table: 4-byte region length, then {id:2, off:2} ... 0x00000000
        proc_region_len = self.gcx.read_int()
        region_start = self.gcx.offset  # offset after the region-length header
        while True:
            proc_id = self.gcx.read_short()
            proc_offset = self.gcx.read_short()
            if proc_id == 0 and proc_offset == 0:
                break
            self.procedures.append({"id": proc_id, "offset": proc_offset})

        # 2. Proc bodies. The runtime (command.c:93) computes each proc's
        #    absolute start as proc_body + proc.offset. proc_body is the byte
        #    after the proc-table terminator; this is also `region_start +
        #    (num_entries + 1) * 4`. For a zero-proc file, proc_body sits
        #    right after the terminator, matching the runtime's set_proc_table
        #    return value.
        proc_body_start = self.gcx.offset
        for proc in self.procedures:
            self.gcx.offset = proc_body_start + proc["offset"]
            self.tree_data.append(
                GclNode({
                    "PROC_ID": proc["id"],
                    "PROC_DATA": self._decompile_node(),
                })
            )

        # 3. Main script body: starts at region_start + proc_region_len,
        #    preceded by a 4-byte script-region-length prefix.
        self.gcx.offset = region_start + proc_region_len
        script_region_len = self.gcx.read_int()
        script_start = self.gcx.offset
        self.tree_data.append(
            GclNode({"PROC_ID": None, "PROC_DATA": self._decompile_node()})
        )

        # 4. Trailing data (fonts / whatever). Record size; don't parse.
        self.gcx.offset = script_start + script_region_len
        self.fonts_size = len(self.gcx) - self.gcx.offset

    # ------------------------------------------------------------------ core

    def _current_command(self):
        return self.commands_stack[-1] if self.commands_stack else None

    def _decompile_node(self):
        gcl_code = self.gcx.read_byte()

        # Variable family: 0x11..0x18 — low nibble encodes inner type.
        if (gcl_code & 0xF0) == GclCode.VAR.value:
            raw = "".join("%02X" % self.gcx.read_byte() for _ in range(3))
            inner = GclCode(gcl_code & 0x0F).name
            return GclNode({GclCode.VAR.name: GclNode({inner: raw})})

        value = None
        match gcl_code:
            case GclCode.GCL_NULL.value:
                return None

            case GclCode.WORD.value:
                value = self.gcx.read_short()
                # runtime casts to signed short in parse.c:41
                if value >= 0x8000:
                    value -= 0x10000

            case GclCode.BYTE.value:
                value = self.gcx.read_byte()

            case GclCode.CHAR.value:
                value = chr(self.gcx.read_byte())

            case GclCode.FLAG.value:
                value = self.gcx.read_byte() == 1

            case GclCode.STR_ID.value:
                value = self.gcx.read_short()

            case GclCode.STR.value:
                size = self.gcx.read_byte()
                value = self.gcx.read_string(length=size)

            case GclCode.PROC.value:
                value = self.gcx.read_short()

            case GclCode.SD_CODE.value:
                value = self.gcx.read_int()

            case GclCode.TABLE.value:
                value = self.gcx.read_int()

            case GclCode.ARG.value:
                value = self.gcx.read_byte()

            case GclCode.EXPR.value:
                # size = length of expression payload including terminator,
                # minus 1 (parse.c:84 advances ptr by raw size).
                size = self.gcx.read_byte() - 1
                end = self.gcx.offset + size
                operands = []
                while self.gcx.offset < end:
                    op = self._decompile_node()
                    if op is None:
                        break
                    t, v = op.get_pair()
                    if t == GclCode.OP.name:
                        if v == GclOperator.OP_NULL.name:
                            break
                        # Runtime (expr.c:87) always pops 2 operands per op,
                        # even for unary — the unary calc ignores sp[-2]
                        # and `sp--` drops it. Mirror that here; the writer
                        # drops the dummy when rendering unary.
                        popped = operands[-2:]
                        del operands[-2:]
                        operands.append(
                            GclNode({GclCode.OP.name: GclNode({v: popped})})
                        )
                    else:
                        operands.append(op)
                value = operands

            case GclCode.OP.value:
                value = GclOperator(self.gcx.read_byte()).name

            case GclCode.SCRIPT.value:
                size = self.gcx.read_short() - 2
                end = self.gcx.offset + size
                value = []
                while self.gcx.offset < end:
                    child = self._decompile_node()
                    if child is None:
                        break
                    value.append(child)

            case GclCode.OPTION.value:
                opt_letter = chr(self.gcx.read_byte())
                size = self.gcx.read_byte() - 1
                data = []
                # Walk until we hit NULL or the next OPTION at the parent
                # level. Can't rely on `size` alone because the runtime also
                # doesn't (see gcl_decompile.py:215 from the reference tool).
                while True:
                    peek = self.gcx.read_byte(self.gcx.offset)
                    if not peek or peek == GclCode.OPTION.value:
                        break
                    pre_offset = self.gcx.offset
                    val = self._decompile_node()
                    if val is None:
                        break
                    # Brace-less elseif/else: the inner SCRIPT's own size
                    # extends one past the option's size, signaling that
                    # the compiler stripped the trailing NUL.
                    if (self._current_command() == GclCommand.IF.value
                            and val.get_pair()[0] == GclCode.SCRIPT.name):
                        script_size = self.gcx.read_short(pre_offset + 1)
                        if script_size + 2 - size == 1:
                            val["NO_BRACES"] = True
                    data.append(val)
                value = GclNode({opt_letter: data})
                if size == -1 and data:
                    value["NULL_SIZE"] = True

            case GclCode.CMD.value:
                size = self.gcx.read_short() - 2
                end = self.gcx.offset + size
                command_id = self.gcx.read_short()
                self.commands_stack.append(command_id)

                args_size = self.gcx.read_byte() - 1
                args_end = self.gcx.offset + args_size
                args = []
                while True:
                    if self.gcx.read_byte(self.gcx.offset) == 0:
                        break
                    arg = self._decompile_node()
                    if arg is None:
                        break
                    args.append(arg)

                # `if` without braces in source leaves a one-byte delta.
                if command_id == GclCommand.IF.value and args_end - self.gcx.offset == 0:
                    if len(args) >= 2:
                        args[1]["NO_BRACES"] = True

                options = []
                while self.gcx.offset < end:
                    opt = self._decompile_node()
                    if opt is None:
                        break
                    options.append(opt)

                try:
                    cmd_name = GclCommand(command_id).name
                except ValueError:
                    cmd_name = "CMD_%04X" % command_id
                value = GclNode({cmd_name: args + options})
                self.commands_stack.pop()

            case GclCode.CALL.value:
                size = self.gcx.read_byte() - 1
                end = self.gcx.offset + size
                proc_id = str(self.gcx.read_short())
                proc_args = []
                while self.gcx.offset < end:
                    arg = self._decompile_node()
                    if arg is None:
                        break
                    proc_args.append(arg)
                value = GclNode({proc_id: proc_args})

            case _:
                raise ValueError(
                    f"unknown GCL opcode 0x{gcl_code:02X} at offset "
                    f"{self.gcx.offset - 1}"
                )

        return GclNode({GclCode(gcl_code).name: value})
