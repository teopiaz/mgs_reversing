"""Pretty-print a GclNode AST as .gcl source text.

Format is inspired by the third-party GCL_Converter v0.2 output (readable
DSL with braces, sigil variables, named parameters) but is unambiguous
and round-trip-ready: no silent `bytes:` gap for undecoded opcodes.
"""
import textwrap

from .constants import (
    COMMAND_NAME,
    GclCode,
    GclCommand,
    GclOperator,
    OPERATOR_SYMBOL,
)

INDENT = "    "
PROC_PREFIX = "sub"

# Operator precedence: lower = binds looser. Mirrors C so the output reads
# naturally. Values aren't compared to C-ints — only to each other.
_PRECEDENCE = {
    GclOperator.ASSIGN.name:             1,
    GclOperator.OR.name:                 2,
    GclOperator.AND.name:                3,
    GclOperator.BITWISEOR.name:          4,
    GclOperator.BITWISEXOR.name:         5,
    GclOperator.BITWISEAND.name:         6,
    GclOperator.EQUALS.name:             7,
    GclOperator.NOTEQUALS.name:          7,
    GclOperator.LESSTHAN.name:           8,
    GclOperator.LESSTHANOREQUAL.name:    8,
    GclOperator.GREATERTHAN.name:        8,
    GclOperator.GREATERTHANOREQUAL.name: 8,
    GclOperator.ADD.name:                9,
    GclOperator.SUBTRACT.name:           9,
    GclOperator.MULTIPLY.name:           10,
    GclOperator.DIVIDE.name:             10,
    GclOperator.MODULUS.name:            10,
    GclOperator.NEGATE.name:             11,
    GclOperator.ISFALSE.name:            11,
    GclOperator.COMPLEMENT.name:         11,
}


def _op_precedence(node):
    if node is None:
        return None
    t, v = node.get_pair()
    if t != GclCode.OP.name:
        return None
    op_name, _ = v.get_pair()
    return _PRECEDENCE.get(op_name)


class GclWriter:
    def __init__(self, tree_data, fonts_size=0, use_names=False):
        self.tree_data = tree_data
        self.fonts_size = fonts_size
        self.use_names = use_names
        self._name_table = None
        if use_names:
            try:
                from .names import HASH_TO_NAME
                self._name_table = HASH_TO_NAME
            except ImportError:
                self._name_table = {}

    def render(self) -> str:
        out = []
        out.append(f"# GCL decompiler output (tools/mgs_tools/gcl)\n")
        out.append(f"# {len(self.tree_data)} top-level block(s)\n\n")
        for node in self.tree_data:
            out.append(self._render_top(node))
            out.append("\n")
        if self.fonts_size:
            out.append(f"\n# Trailing data: {self.fonts_size} bytes (font/extra)\n")
        return "".join(out)

    # ------------------------------------------------------------ top-level

    def _render_top(self, node: dict) -> str:
        if "PROC_DATA" not in node:
            return self._render(node)

        pid = node["PROC_ID"]
        body = node["PROC_DATA"]

        if pid is None:
            # The unnamed main script block.
            header = "script"
        else:
            header = f"proc {PROC_PREFIX}_{pid:04X}"

        return f"{header} " + self._render(body)

    # ------------------------------------------------------------ dispatch

    def _render(self, node) -> str:
        if node is None:
            return ""
        t, v = node.get_pair()

        match t:
            case GclCode.WORD.name:
                return str(v)
            case GclCode.BYTE.name:
                # `b:` prefix lets the parser round-trip back to BYTE.
                return f"b:{v}"
            case GclCode.CHAR.name:
                return f"'{v}'"
            case GclCode.FLAG.name:
                return "true" if v else "false"
            case GclCode.STR_ID.name:
                if self._name_table is not None:
                    name = self._name_table.get(v)
                    if name is not None:
                        return f"&{name}"
                return f"$s:{v:04x}"
            case GclCode.STR.name:
                # Prefix `m` when the string contains MGS-encoded 2-byte
                # markers (`{XXXX}` or table-mapped chars). Pure ASCII
                # strings keep the plain `"..."` form.
                needs_m = any(ord(c) > 0x7F for c in v) or "{" in v
                prefix = "m" if needs_m else ""
                return f'{prefix}"{v}"'
            case GclCode.PROC.name:
                return f"{PROC_PREFIX}_{v:04X}"
            case GclCode.SD_CODE.name:
                return f"sd:{v:08X}"
            case GclCode.TABLE.name:
                return f"t:{v:08X}"
            case GclCode.VAR.name:
                return self._render_var(v)
            case GclCode.ARG.name:
                return f"arg{v}"
            case GclCode.EXPR.name:
                return self._render_expr(v)
            case GclCode.OP.name:
                return self._render_op(v)
            case GclCode.SCRIPT.name:
                return self._render_script(v, no_braces=node.get("NO_BRACES", False))
            case GclCode.OPTION.name:
                return self._render_option(v)
            case GclCode.CMD.name:
                return self._render_cmd(v)
            case GclCode.CALL.name:
                return self._render_call(v)

        return f"/* unknown:{t} */"

    # ------------------------------------------------------------ families

    def _render_var(self, inner):
        # inner is a GclNode like {'BYTE': 'XXXXXX'} — 3-byte hex raw.
        inner_t, inner_v = inner.get_pair()
        sigil = {
            GclCode.WORD.name:   "w",
            GclCode.BYTE.name:   "b",
            GclCode.CHAR.name:   "c",
            GclCode.FLAG.name:   "f",
            GclCode.STR_ID.name: "s",
            GclCode.PROC.name:   "p",
        }.get(inner_t, "?")
        return f"${sigil}:{inner_v}"

    def _render_expr(self, operands):
        # Expression wraps a single top-level OP (or a bare value). Let the
        # caller (eval/if/option) add outer parens as needed.
        return "".join(self._render(op) for op in operands)

    def _render_op(self, op_node):
        op_name, operands = op_node.get_pair()
        sym = OPERATOR_SYMBOL[op_name]
        op_enum = GclOperator[op_name]

        # Unary: the runtime pushes a dummy before unary ops (see
        # expr.c:87); decompiler preserves both. Render only operand[1].
        # Parens force parser to produce a NEGATE OP rather than folding
        # into a negative WORD literal (the two encodings aren't
        # interchangeable — they emit different bytecode).
        if op_enum.value < 4:
            target = operands[1] if len(operands) >= 2 else (operands[0] if operands else None)
            return f"{sym}({self._render(target)})"

        left, right = operands[0], operands[1]
        lp = _op_precedence(left)
        rp = _op_precedence(right)
        my_prec = _PRECEDENCE[op_name]
        lrender = self._render(left)
        rrender = self._render(right)
        if lp is not None and lp < my_prec:
            lrender = f"({lrender})"
        if rp is not None and rp <= my_prec and op_name != GclOperator.ASSIGN.name:
            # Right-associative for =; otherwise parenthesize equal-precedence
            # right children so subtraction/division read correctly.
            rrender = f"({rrender})"
        return f"{lrender} {sym} {rrender}"

    def _render_script(self, stmts, no_braces=False):
        if not stmts:
            return "{}\n" if not no_braces else "\n"
        body_lines = []
        for s in stmts:
            text = self._render(s)
            if not text.endswith("\n"):
                text += "\n"
            body_lines.append(text)
        body = textwrap.indent("".join(body_lines), INDENT)
        if no_braces:
            return body
        return "{\n" + body + "}\n"

    def _render_option(self, opt_node):
        letter, values = opt_node.get_pair()
        parts = [self._render(v) for v in values]
        # Join non-block values on one line; scripts inline their braces.
        inline = []
        block = []
        for v, text in zip(values, parts):
            vt, _ = v.get_pair()
            if vt == GclCode.SCRIPT.name:
                block.append(text)
            else:
                inline.append(text)
        # NULL_SIZE options encode a zero size byte instead of the real
        # payload length. Marked with a trailing '!' so the parser can
        # restore the flag.
        prefix = "-!" if opt_node.get("NULL_SIZE") else "-"
        head = f"{prefix}{letter}"
        if inline:
            head += " " + " ".join(inline)
        if block:
            head += " " + "".join(block).rstrip()
        return head

    def _render_cmd(self, cmd_node):
        name, items = cmd_node.get_pair()
        lname = name.lower() if name.startswith("CMD_") is False else name
        # Special shapes: if / eval / call already handled structurally.
        if name == GclCommand.IF.name:
            return self._render_if(items)
        if name == GclCommand.EVAL.name:
            # EVAL always wraps a single expression.
            if items:
                return f"eval({self._render(items[0])})"
            return "eval()"

        head = lname
        positional = []
        options = []
        for it in items:
            it_t, _ = it.get_pair()
            if it_t == GclCode.OPTION.name:
                options.append(it)
            else:
                positional.append(it)

        parts = [head]
        parts.extend(self._render(p) for p in positional)
        line = " ".join(parts)
        if options:
            opt_txt = "\n".join(INDENT + self._render(o).rstrip() for o in options)
            line += " \\\n" + opt_txt
        return line + "\n"

    def _render_if(self, items):
        # items layout: [cond, body, (option 'i' or 'e')...]
        if len(items) < 2:
            return "if /* malformed */\n"
        cond = self._render(items[0])
        body = self._render(items[1])
        out = [f"if ({cond}) {body.rstrip()}"]
        for extra in items[2:]:
            t, v = extra.get_pair()
            if t != GclCode.OPTION.name:
                out.append(f" /* unexpected {t} */")
                continue
            letter, payload = v.get_pair()
            if letter == "i" and len(payload) >= 2:
                c = self._render(payload[0])
                b = self._render(payload[1])
                out.append(f" elseif ({c}) {b.rstrip()}")
            elif letter == "e" and payload:
                b = self._render(payload[0])
                out.append(f" else {b.rstrip()}")
        return "".join(out) + "\n"

    def _render_call(self, call_node):
        pid_str, args = call_node.get_pair()
        pid = int(pid_str)
        rendered_args = [f"{PROC_PREFIX}_{pid:04X}"]
        rendered_args.extend(self._render(a) for a in args)
        return f"call({', '.join(rendered_args)})\n"
