"""Parse .gcl source text into a GclNode AST.

Accepts the output of gcl_writer.py and feeds gcl_compile.py.

Grammar (roughly):
    file         := topLevel*
    topLevel     := 'proc' IDENT block | 'script' block | 'FONTS' hexList
    block        := '{' stmt* '}'
    stmt         := if_stmt | eval_stmt | call_stmt | cmd_stmt
    if_stmt      := 'if' '(' expr ')' (block | stmt) (elseif)* (else)?
    elseif       := 'elseif' '(' expr ')' (block | stmt)
    else         := 'else'  (block | stmt)
    eval_stmt    := 'eval' '(' expr ')'
    call_stmt    := 'call' '(' IDENT (',' expr)* ')'
    cmd_stmt     := IDENT expr* ('\\' '-' letter expr*)*
    expr         := assignOp                       (precedence climbing)
    ...

Tokens are whitespace/newline separated except inside strings.
"""
import re
import sys

from constants import GclCode, GclCommand, GclOperator
from gcx import GclNode


# ---------------------------------------------------------------------- lex

_TOKEN_RE = re.compile(
    r"""
    [ \t]*(?:                                       # horizontal ws only
        (?P<COMMENT>\#[^\n]*)                      # '#' line comment
      | (?P<CONT>\\[ \t]*(?:\#[^\n]*)?\n)           # backslash + (optional ws + comment) + newline
      | (?P<NEWLINE>\n)                             # significant newline
      | (?P<M_STR>m"(?:[^"\\]|\\.)*")               # m"..." MGS-encoded string
      | (?P<STR>"(?:[^"\\]|\\.)*")                  # "..." ASCII string
      | (?P<CHAR>'(?:[^'\\]|\\.)')                  # 'c' char
      | (?P<VAR>\$[wbfscp]:[0-9A-Fa-f]+)            # $w:XXXXXX variable
      | (?P<BYTE>b:\d+)                             # b:N byte literal (positive only; negation is unary)
      | (?P<SD>sd:[0-9A-Fa-f]+)                     # sd:XXXXXXXX
      | (?P<TAB>t:[0-9A-Fa-f]+)                     # t:XXXXXXXX
      | (?P<NAMEREF>&[A-Za-z_][A-Za-z_0-9]*)         # &NAME symbolic constant (mgs_names table)
      | (?P<PROCREF>sub_[0-9A-Fa-f]+)               # sub_XXXX (proc ref)
      | (?P<ARG>arg\d+)                             # argN
      | (?P<HEX>0x[0-9A-Fa-f]+)                     # 0xNNNN
      | (?P<INT>\d+)                                # decimal integer (non-negative)
      | (?P<IDENT>[A-Za-z_][A-Za-z_0-9]*)           # identifier / keyword
      | (?P<DASH_OPT>-!?[A-Za-z](?!:))              # -l option header (-! marks NULL_SIZE)
      | (?P<SYM>==|!=|<=|>=|\|\||&&|[-+*/%&|^~!<>=(){},])  # symbols
    )
    """,
    re.VERBOSE,
)


class Token:
    __slots__ = ("kind", "text", "pos")

    def __init__(self, kind, text, pos):
        self.kind = kind
        self.text = text
        self.pos = pos

    def __repr__(self):
        return f"Token({self.kind!r}, {self.text!r}@{self.pos})"


def tokenize(src: str):
    tokens = []
    i = 0
    while i < len(src):
        m = _TOKEN_RE.match(src, i)
        if not m:
            # skip unrecognized whitespace
            if src[i].isspace():
                i += 1
                continue
            raise SyntaxError(f"unexpected character {src[i]!r} at offset {i}")
        kind = m.lastgroup
        text = m.group(kind)
        if kind not in ("COMMENT", "CONT"):
            tokens.append(Token(kind, text, m.start(kind)))
        i = m.end()
    tokens.append(Token("EOF", "", i))
    return tokens


# ---------------------------------------------------------------------- parse


SIGIL_TO_CODE = {
    "w": GclCode.WORD,
    "b": GclCode.BYTE,
    "f": GclCode.FLAG,
    "s": GclCode.STR_ID,
    "c": GclCode.CHAR,
    "p": GclCode.PROC,
}

BINARY_OPS = {
    "*":  GclOperator.MULTIPLY,
    "/":  GclOperator.DIVIDE,
    "%":  GclOperator.MODULUS,
    "+":  GclOperator.ADD,
    "-":  GclOperator.SUBTRACT,
    "<":  GclOperator.LESSTHAN,
    "<=": GclOperator.LESSTHANOREQUAL,
    ">":  GclOperator.GREATERTHAN,
    ">=": GclOperator.GREATERTHANOREQUAL,
    "==": GclOperator.EQUALS,
    "!=": GclOperator.NOTEQUALS,
    "&":  GclOperator.BITWISEAND,
    "^":  GclOperator.BITWISEXOR,
    "|":  GclOperator.BITWISEOR,
    "&&": GclOperator.AND,
    "||": GclOperator.OR,
    "=":  GclOperator.ASSIGN,
}

BINARY_PREC = {
    GclOperator.ASSIGN:             1,
    GclOperator.OR:                 2,
    GclOperator.AND:                3,
    GclOperator.BITWISEOR:          4,
    GclOperator.BITWISEXOR:         5,
    GclOperator.BITWISEAND:         6,
    GclOperator.EQUALS:             7,
    GclOperator.NOTEQUALS:          7,
    GclOperator.LESSTHAN:           8,
    GclOperator.LESSTHANOREQUAL:    8,
    GclOperator.GREATERTHAN:        8,
    GclOperator.GREATERTHANOREQUAL: 8,
    GclOperator.ADD:                9,
    GclOperator.SUBTRACT:           9,
    GclOperator.MULTIPLY:           10,
    GclOperator.DIVIDE:             10,
    GclOperator.MODULUS:            10,
}

UNARY_OPS = {
    "-": GclOperator.NEGATE,
    "!": GclOperator.ISFALSE,
    "~": GclOperator.COMPLEMENT,
}


class Parser:
    def __init__(self, tokens):
        self.t = tokens
        self.i = 0

    # ----------- utilities

    def peek(self, offset=0):
        return self.t[self.i + offset]

    def eat(self):
        tok = self.t[self.i]
        self.i += 1
        return tok

    def skip_newlines(self):
        while self.peek().kind == "NEWLINE":
            self.i += 1

    def expect(self, kind, text=None):
        tok = self.peek()
        if tok.kind != kind or (text is not None and tok.text != text):
            raise SyntaxError(
                f"expected {kind}{'='+text if text else ''}, got {tok}"
            )
        return self.eat()

    def at_sym(self, text):
        tok = self.peek()
        return tok.kind == "SYM" and tok.text == text

    def at_kw(self, word):
        tok = self.peek()
        return tok.kind == "IDENT" and tok.text == word

    # ----------- entry

    def parse_file(self):
        tree = []
        self.skip_newlines()
        while self.peek().kind != "EOF":
            if self.at_kw("proc"):
                tree.append(self._parse_proc())
            elif self.at_kw("script"):
                tree.append(self._parse_script_toplevel())
            elif self.at_kw("FONTS"):
                tree.append(self._parse_fonts())
            else:
                raise SyntaxError(f"expected 'proc'/'script'/'FONTS', got {self.peek()}")
            self.skip_newlines()
        return tree

    def _parse_proc(self):
        self.eat()                                  # 'proc'
        name_tok = self.expect("PROCREF")
        pid = int(name_tok.text[4:], 16)
        body = self._parse_script_block()
        return GclNode({"PROC_ID": pid, "PROC_DATA": body})

    def _parse_script_toplevel(self):
        self.eat()                                  # 'script'
        body = self._parse_script_block()
        return GclNode({"PROC_ID": None, "PROC_DATA": body})

    def _parse_fonts(self):
        self.eat()                                  # 'FONTS'
        self.expect("SYM", "(")
        hexes = []
        while not self.at_sym(")"):
            h = self.expect("IDENT").text  # hex chars parse as IDENT if alpha
            hexes.append(h)
            if self.at_sym(","):
                self.eat()
        self.eat()                                  # ')'
        return GclNode({"FONTS": hexes})

    # ----------- statements / blocks

    def _parse_script_block(self):
        """Parse '{' stmt* '}' or a single stmt (no braces).

        Returns a SCRIPT GclNode.
        """
        no_braces = False
        if self.at_sym("{"):
            self.eat()
            self.skip_newlines()
            stmts = []
            while not self.at_sym("}"):
                self.skip_newlines()
                if self.at_sym("}"):
                    break
                stmts.append(self._parse_stmt())
                self.skip_newlines()
            self.eat()                              # '}'
        else:
            no_braces = True
            stmts = [self._parse_stmt()]
        node = GclNode({GclCode.SCRIPT.name: stmts})
        if no_braces:
            node["NO_BRACES"] = True
        return node

    def _parse_stmt(self):
        tok = self.peek()
        if tok.kind == "IDENT" and tok.text == "if":
            return self._parse_if()
        if tok.kind == "IDENT" and tok.text == "eval":
            return self._parse_eval()
        if tok.kind == "IDENT" and tok.text == "call":
            return self._parse_call()
        if tok.kind == "IDENT":
            return self._parse_cmd()
        raise SyntaxError(f"expected statement, got {tok}")

    def _parse_if(self):
        self.eat()                                  # 'if'
        self.expect("SYM", "(")
        cond = self._parse_expr_node()
        self.expect("SYM", ")")
        self.skip_newlines()
        body = self._parse_script_block()

        items = [cond, body]
        # Chain of elseif / else.
        while True:
            self.skip_newlines()
            if self.at_kw("elseif"):
                self.eat()
                self.expect("SYM", "(")
                ec = self._parse_expr_node()
                self.expect("SYM", ")")
                self.skip_newlines()
                eb = self._parse_script_block()
                opt = GclNode({GclCode.OPTION.name: GclNode({"i": [ec, eb]})})
                items.append(opt)
            elif self.at_kw("else"):
                self.eat()
                self.skip_newlines()
                eb = self._parse_script_block()
                opt = GclNode({GclCode.OPTION.name: GclNode({"e": [eb]})})
                items.append(opt)
                break
            else:
                break

        return GclNode({
            GclCode.CMD.name: GclNode({GclCommand.IF.name: items})
        })

    def _parse_eval(self):
        self.eat()                                  # 'eval'
        self.expect("SYM", "(")
        expr = self._parse_expr_node()
        self.expect("SYM", ")")
        return GclNode({
            GclCode.CMD.name: GclNode({GclCommand.EVAL.name: [expr]})
        })

    def _parse_call(self):
        self.eat()                                  # 'call'
        self.expect("SYM", "(")
        target = self.expect("PROCREF").text
        pid = int(target[4:], 16)
        args = []
        while self.at_sym(","):
            self.eat()
            # Call args are raw operands (VAR / ARG / literal), not EXPRs.
            args.append(self._parse_value())
        self.expect("SYM", ")")
        return GclNode({
            GclCode.CALL.name: GclNode({str(pid): args})
        })

    def _parse_cmd(self):
        name_tok = self.eat()
        name = name_tok.text.upper()
        if name not in GclCommand.__members__:
            if not name.startswith("CMD_"):
                raise SyntaxError(f"unknown command {name_tok.text!r} at {name_tok.pos}")
            name = name_tok.text
        items = []
        # Positional args: until newline, DASH_OPT, or close brace.
        while True:
            tok = self.peek()
            if tok.kind in ("NEWLINE", "EOF"):
                break
            if tok.kind == "DASH_OPT":
                break
            if tok.kind == "SYM" and tok.text == "}":
                break
            items.append(self._parse_value())

        # Options may span multiple lines; a command ends when we see an
        # IDENT (next statement) or '}' (end of block).
        while True:
            # Peek past any intervening newlines without consuming them yet.
            lookahead = self.i
            while (lookahead < len(self.t)
                   and self.t[lookahead].kind == "NEWLINE"):
                lookahead += 1
            nxt = self.t[lookahead]
            if nxt.kind != "DASH_OPT":
                break
            self.i = lookahead                          # commit skip
            opt_tok = self.eat()
            marker = opt_tok.text[1:]
            null_size = marker.startswith("!")
            letter = marker[1:] if null_size else marker
            opt_values = []
            while True:
                t = self.peek()
                if t.kind in ("NEWLINE", "EOF") or t.kind == "DASH_OPT":
                    break
                if t.kind == "SYM" and t.text == "}":
                    break
                # `call(...)` / `if(...)` / unknown IDENT → either a
                # call-expression value or the start of the next stmt.
                if t.kind == "IDENT":
                    if t.text == "call":
                        opt_values.append(self._parse_call())
                        continue
                    if t.text in ("true", "false"):
                        opt_values.append(self._parse_operand())
                        continue
                    # Other IDENTs mean the next statement has begun.
                    break
                opt_values.append(self._parse_value())
            opt_body = GclNode({letter: opt_values})
            if null_size:
                opt_body["NULL_SIZE"] = True
            items.append(GclNode({GclCode.OPTION.name: opt_body}))

        return GclNode({
            GclCode.CMD.name: GclNode({name: items})
        })

    # ----------- expressions

    def _parse_expr_node(self):
        """Return an EXPR GclNode whose body wraps one top-level tree.

        Decompiler form: EXPR body is `[top_node]` where top_node is the
        final OP tree (or a bare operand for a trivial expression).
        """
        top = self._parse_or()
        return GclNode({GclCode.EXPR.name: [top]})

    def _parse_or(self):
        left = self._parse_and()
        while self.at_sym("||"):
            self.eat()
            right = self._parse_and()
            left = _make_binop(GclOperator.OR, left, right)
        return left

    def _parse_and(self):
        left = self._parse_bitor()
        while self.at_sym("&&"):
            self.eat()
            right = self._parse_bitor()
            left = _make_binop(GclOperator.AND, left, right)
        return left

    def _parse_bitor(self):
        left = self._parse_bitxor()
        while self.at_sym("|") and not self.at_sym("||"):
            self.eat()
            right = self._parse_bitxor()
            left = _make_binop(GclOperator.BITWISEOR, left, right)
        return left

    def _parse_bitxor(self):
        left = self._parse_bitand()
        while self.at_sym("^"):
            self.eat()
            right = self._parse_bitand()
            left = _make_binop(GclOperator.BITWISEXOR, left, right)
        return left

    def _parse_bitand(self):
        left = self._parse_equality()
        while self.at_sym("&") and not self.at_sym("&&"):
            self.eat()
            right = self._parse_equality()
            left = _make_binop(GclOperator.BITWISEAND, left, right)
        return left

    def _parse_equality(self):
        left = self._parse_relational()
        while self.at_sym("==") or self.at_sym("!="):
            op = self.eat().text
            right = self._parse_relational()
            left = _make_binop(BINARY_OPS[op], left, right)
        return left

    def _parse_relational(self):
        left = self._parse_additive()
        while (self.at_sym("<") or self.at_sym("<=")
               or self.at_sym(">") or self.at_sym(">=")):
            op = self.eat().text
            right = self._parse_additive()
            left = _make_binop(BINARY_OPS[op], left, right)
        return left

    def _parse_additive(self):
        left = self._parse_mult()
        while self.at_sym("+") or self.at_sym("-"):
            op = self.eat().text
            right = self._parse_mult()
            left = _make_binop(BINARY_OPS[op], left, right)
        return left

    def _parse_mult(self):
        left = self._parse_unary()
        while self.at_sym("*") or self.at_sym("/") or self.at_sym("%"):
            op = self.eat().text
            right = self._parse_unary()
            left = _make_binop(BINARY_OPS[op], left, right)
        return left

    def _parse_unary(self):
        if self.peek().kind == "SYM" and self.peek().text in UNARY_OPS:
            op_text = self.eat().text
            # Fold `-N` or `-0xN` into a negative WORD literal. The writer
            # uses parens `-(N)` to force the NEGATE OP form.
            nxt = self.peek()
            if op_text == "-" and nxt.kind in ("INT", "HEX"):
                tok = self.eat()
                v = int(tok.text, 16 if tok.kind == "HEX" else 10)
                return GclNode({GclCode.WORD.name: -v})
            operand = self._parse_unary()
            # Runtime needs a dummy operand before the unary op (expr.c:87).
            dummy = GclNode({GclCode.BYTE.name: 0})
            return GclNode({
                GclCode.OP.name: GclNode({
                    UNARY_OPS[op_text].name: [dummy, operand]
                })
            })
        return self._parse_primary()

    def _parse_primary(self):
        # Assignment: lhs = rhs handled here because = is right-associative
        # and lives at the very bottom.
        left = self._parse_assign_target()
        if self.at_sym("="):
            self.eat()
            right = self._parse_or()
            return _make_binop(GclOperator.ASSIGN, left, right)
        return left

    def _parse_assign_target(self):
        # Distinguish parenthesized expressions from simple operands.
        if self.at_sym("("):
            self.eat()
            inner = self._parse_or()
            self.expect("SYM", ")")
            return inner
        return self._parse_operand()

    def _parse_value(self):
        """A command arg / option value. Allows unary prefix (e.g. -3952).

        In option/arg context, a unary `-` before a plain integer folds
        into a negative WORD literal (the runtime's `parse_value` only
        handles opcode values, not OP nodes). Inside expressions we keep
        the NEGATE OP form (see `_parse_unary`).
        """
        if self.peek().kind == "SYM" and self.peek().text == "-":
            # Peek one ahead to see if we can fold.
            nxt = self.peek(1)
            if nxt.kind in ("INT", "HEX"):
                self.eat()                              # '-'
                n_tok = self.eat()
                v = int(n_tok.text, 16 if n_tok.kind == "HEX" else 10)
                return GclNode({GclCode.WORD.name: -v})
            # Fallback: nested negations etc.
            self.eat()                                  # '-'
            operand = self._parse_value()
            dummy = GclNode({GclCode.BYTE.name: 0})
            return GclNode({
                GclCode.OP.name: GclNode({
                    GclOperator.NEGATE.name: [dummy, operand]
                })
            })
        return self._parse_operand()

    def _parse_operand(self):
        """A single atomic value: literal, variable, string, proc ref, etc."""
        tok = self.peek()
        k, text = tok.kind, tok.text

        if k == "INT":
            self.eat()
            return GclNode({GclCode.WORD.name: int(text)})

        if k == "HEX":
            self.eat()
            return GclNode({GclCode.WORD.name: int(text, 16)})

        if k == "BYTE":
            self.eat()
            return GclNode({GclCode.BYTE.name: int(text[2:])})

        if k == "IDENT":
            if text == "true":
                self.eat()
                return GclNode({GclCode.FLAG.name: True})
            if text == "false":
                self.eat()
                return GclNode({GclCode.FLAG.name: False})
            # Bare identifier inside an option position → treat as STR_ID
            # of its hash. (Not currently emitted by our writer; accept
            # for future hash-resolved source.)
            from constants import gv_strcode
            self.eat()
            return GclNode({GclCode.STR_ID.name: gv_strcode(text)})

        if k == "STR":
            self.eat()
            # Strip the outer quotes; keep \xNN escapes literal.
            return GclNode({GclCode.STR.name: text[1:-1]})

        if k == "M_STR":
            self.eat()
            # m"..." — MGS text. Keep payload verbatim; `{XXXX}` markers
            # and table chars are expanded by gcl_compile._encode_string.
            return GclNode({GclCode.STR.name: text[2:-1]})

        if k == "CHAR":
            self.eat()
            return GclNode({GclCode.CHAR.name: text[1:-1]})

        if k == "VAR":
            self.eat()
            # $<sigil>:<hex>. Length: 4 → STR_ID literal; 6 → VAR.
            sigil = text[1]
            hexpart = text[3:]
            if len(hexpart) == 4 and sigil == "s":
                return GclNode({GclCode.STR_ID.name: int(hexpart, 16)})
            inner = SIGIL_TO_CODE[sigil].name
            # Normalize to 6-hex uppercase for the VAR payload.
            hex6 = hexpart.upper().zfill(6)
            return GclNode({GclCode.VAR.name: GclNode({inner: hex6})})

        if k == "SD":
            self.eat()
            return GclNode({GclCode.SD_CODE.name: int(text[3:], 16)})

        if k == "TAB":
            self.eat()
            return GclNode({GclCode.TABLE.name: int(text[2:], 16)})

        if k == "PROCREF":
            self.eat()
            return GclNode({GclCode.PROC.name: int(text[4:], 16)})

        if k == "NAMEREF":
            from mgs_names import NAME_TO_HASH
            self.eat()
            ident = text[1:]                      # strip leading &
            if ident not in NAME_TO_HASH:
                raise SyntaxError(
                    f"unknown symbolic name '&{ident}' at offset {tok.pos} — "
                    "add it to port/gcl_tools/mgs_names.py or use the raw "
                    "hash literal `$s:XXXX` instead"
                )
            return GclNode({GclCode.STR_ID.name: NAME_TO_HASH[ident]})

        if k == "ARG":
            self.eat()
            return GclNode({GclCode.ARG.name: int(text[3:])})

        if k == "SYM" and text == "(":
            self.eat()
            inner = self._parse_or()
            self.expect("SYM", ")")
            return inner

        if k == "SYM" and text == "{":
            # A SCRIPT block used as an option value (e.g. trap -e {...}).
            return self._parse_script_block()

        raise SyntaxError(f"unexpected token in expression: {tok}")


def _make_binop(op: GclOperator, left, right):
    """Build a nested OP tree. Compiler recurses to emit operands."""
    return GclNode({
        GclCode.OP.name: GclNode({op.name: [left, right]})
    })


# --------------------------------------------------------------------- entry

def parse(src: str):
    """Parse .gcl source text → AST list."""
    return Parser(tokenize(src)).parse_file()
