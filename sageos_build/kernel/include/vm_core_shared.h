#ifndef SAGE_VM_CORE_SHARED_H
#define SAGE_VM_CORE_SHARED_H

// ============================================================================
// Shared VM Core Definition
// ============================================================================
// Defines shared opcodes and instructions used by both bare-metal and
// standard SageLang VM implementations.

typedef enum {
    OP_CONSTANT       = 0,
    OP_NIL            = 1,
    OP_TRUE           = 2,
    OP_FALSE          = 3,
    OP_POP            = 4,
    OP_GET_GLOBAL     = 5,
    OP_DEFINE_GLOBAL  = 6,
    OP_SET_GLOBAL     = 7,
    OP_DEFINE_FN      = 8,
    OP_GET_PROPERTY   = 9,
    OP_SET_PROPERTY   = 10,
    OP_GET_INDEX      = 11,
    OP_SET_INDEX      = 12,
    OP_SLICE          = 13,
    OP_ADD            = 14,
    OP_SUB            = 15,
    OP_MUL            = 16,
    OP_DIV            = 17,
    OP_MOD            = 18,
    OP_NEGATE         = 19,
    OP_EQUAL          = 20,
    OP_NOT_EQUAL      = 21,
    OP_GREATER        = 22,
    OP_GREATER_EQ     = 23,
    OP_LESS           = 24,
    OP_LESS_EQ        = 25,
    OP_BIT_AND        = 26,
    OP_BIT_OR         = 27,
    OP_BIT_XOR        = 28,
    OP_BIT_NOT        = 29,
    OP_SHIFT_LEFT     = 30,
    OP_SHIFT_RIGHT    = 31,
    OP_NOT            = 32,
    OP_TRUTHY         = 33,
    OP_JUMP           = 34,
    OP_JUMP_IF_FALSE  = 35,
    OP_CALL           = 36,
    OP_CALL_METHOD    = 37,
    OP_ARRAY          = 38,
    OP_TUPLE          = 39,
    OP_DICT           = 40,
    OP_PRINT          = 41,
    OP_EXEC_AST_STMT  = 42,
    OP_RETURN         = 43,
    OP_PUSH_ENV       = 44,
    OP_POP_ENV        = 45,
    OP_DUP            = 46,
    OP_ARRAY_LEN      = 47,
    OP_BREAK          = 48,
    OP_CONTINUE       = 49,
    OP_LOOP_BACK      = 50,
    OP_IMPORT         = 51,
    OP_HALT           = 0xFF
} OpCode;

#endif // SAGE_VM_CORE_SHARED_H
