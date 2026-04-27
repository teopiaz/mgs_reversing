"""GCL opcodes, operators, and known command hashes.

Ground truth: source/libgcl/libgcl.h, source/include/strcode.h.
"""
from enum import Enum


class GclCode(Enum):
    """GCL bytecode opcodes (see source/libgcl/libgcl.h:76)."""
    GCL_NULL = 0x00
    WORD     = 0x01  # $w:
    BYTE     = 0x02  # $b:
    CHAR     = 0x03
    FLAG     = 0x04  # $f:
    STR_ID   = 0x06  # hashed string / proc name
    STR      = 0x07  # length-prefixed raw string
    PROC     = 0x08  # proc reference
    SD_CODE  = 0x09
    TABLE    = 0x0A  # .vox/.dmo/radio.dat reference
    VAR      = 0x10  # variable family (0x11..0x18)
    ARG      = 0x20  # stack arg
    EXPR     = 0x30  # RPN expression
    OP       = 0x31  # expression operator
    SCRIPT   = 0x40  # script/block container
    OPTION   = 0x50  # named parameter (-x ...)
    CMD      = 0x60  # command invocation
    CALL     = 0x70  # proc call


class GclOperator(Enum):
    """GCL expression operators (see source/libgcl/libgcl.h:95)."""
    OP_NULL            = 0
    NEGATE             = 1
    ISFALSE            = 2
    COMPLEMENT         = 3
    ADD                = 4
    SUBTRACT           = 5
    MULTIPLY           = 6
    DIVIDE             = 7
    MODULUS            = 8
    EQUALS             = 9
    NOTEQUALS          = 10
    LESSTHAN           = 11
    LESSTHANOREQUAL    = 12
    GREATERTHAN        = 13
    GREATERTHANOREQUAL = 14
    BITWISEOR          = 15
    BITWISEAND         = 16
    BITWISEXOR         = 17
    OR                 = 18
    AND                = 19
    ASSIGN             = 20


OPERATOR_SYMBOL = {
    GclOperator.NEGATE.name:             "-",
    GclOperator.ISFALSE.name:            "!",
    GclOperator.COMPLEMENT.name:         "~",
    GclOperator.ADD.name:                "+",
    GclOperator.SUBTRACT.name:           "-",
    GclOperator.MULTIPLY.name:           "*",
    GclOperator.DIVIDE.name:             "/",
    GclOperator.MODULUS.name:            "%",
    GclOperator.EQUALS.name:             "==",
    GclOperator.NOTEQUALS.name:          "!=",
    GclOperator.LESSTHAN.name:           "<",
    GclOperator.LESSTHANOREQUAL.name:    "<=",
    GclOperator.GREATERTHAN.name:        ">",
    GclOperator.GREATERTHANOREQUAL.name: ">=",
    GclOperator.BITWISEOR.name:          "|",
    GclOperator.BITWISEAND.name:         "&",
    GclOperator.BITWISEXOR.name:         "^",
    GclOperator.OR.name:                 "||",
    GclOperator.AND.name:                "&&",
    GclOperator.ASSIGN.name:             "=",
}


class GclCommand(Enum):
    """Known command hashes (source/include/strcode.h:13)."""
    IF        = 0x0D86
    EVAL      = 0x64C0
    RETURN    = 0xCD3A
    FOREACH   = 0x7636
    MESG      = 0x22FF
    TRAP      = 0xD4CB
    CHARA     = 0x9906
    MAP       = 0xC091
    MAPDEF    = 0x7D50
    CAMERA    = 0xEEE9
    LIGHT     = 0x306A
    START     = 0x9A1F
    LOAD      = 0xC8BB
    RADIO     = 0x24E1
    RESTART   = 0xE43C
    DEMO      = 0xA242
    NTRAP     = 0xDBAB
    DELAY     = 0x430D
    PAD       = 0xCC85
    VARSAVE   = 0x5C9E
    SYSTEM    = 0x4AD9
    SOUND     = 0x698D
    MENU      = 0x226D
    RAND      = 0x925E
    FUNC      = 0xE257
    DEMODEBUG = 0xA2BF
    PRINT     = 0xB96E
    JIMAKU    = 0xEC9D


COMMAND_NAME = {c.value: c.name.lower() for c in GclCommand}


# gv_strcode now lives in mgs_tools.common.strcode — single source of
# truth shared with the stage authoring side. Re-exported here so older
# `from mgs_tools.gcl.constants import gv_strcode` callers still work.
from mgs_tools.common.strcode import gv_strcode  # noqa: F401
