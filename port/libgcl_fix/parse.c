#include "gcl_ptr_table.h"
void *gcl_ptr_table[GCL_PTR_TABLE_SIZE];
int gcl_ptr_next = 0;
#include "libgcl.h"

#include <stdio.h>
#include <stdint.h>
#include "common.h"

/* 64-bit: preserve full pointer from GCL_GetNextValue for callers that need it */
intptr_t GCL_LastPointerValue = 0;

/* Non-inline wrapper so event.c can resolve ptr table entries */
void *gcl_resolve_ptr_fn(int value) { return gcl_resolve_ptr(value); }

/*** $gp ***/
int            *SECTION(".sbss") argstack_p;
unsigned char **SECTION(".sbss") commandline_p;
unsigned char  *SECTION(".sbss") next_str_ptr;

/*** bss ***/
extern int            argbuffer[32];
extern unsigned char *commandlines[8];

void GCL_SetArgTop(unsigned char *top)
{
    next_str_ptr = top;
}

unsigned char *GCL_GetNextValue(unsigned char *top, int *type_p, int *value_p)
{
    unsigned char *ptr;
    int            gcl_code;
    int            size;

    if (!top) {
        /* 64-bit port: GCL script pointer is NULL — likely a parameter
           was not resolved correctly or the script ran past its end */
        printf("[gcl] WARNING: GCL_GetNextValue called with NULL top\n");
        *type_p = 0; *value_p = 0; return NULL;
    }

    ptr = top;
    gcl_code = *ptr;
    ptr++;
    if (GCL_IsVariable(gcl_code))
    {
        return GCL_GetVar(top, type_p, value_p);
    }
    *type_p = gcl_code;
    switch (gcl_code)
    {
    case GCLCODE_NULL:
        ptr = 0;
        break;

    case GCLCODE_SHORT:
        *value_p = (short)GCL_GetShort(ptr);
        ptr += 2;
        break;

    case GCLCODE_SDCODE:
    case GCLCODE_TABLE_CODE:
        *value_p = (unsigned int)GCL_GetLong(ptr);
        ptr += 4;
        break;

    case GCLCODE_HASHED_STRING:
    case GCLCODE_PROC_CALL:
        *value_p = (unsigned short)GCL_GetShort(ptr);
        ptr += 2;
        break;

    case GCLCODE_BYTE:
    case GCLCODE_CHAR:
    case GCLCODE_FLAG:
        *value_p = (unsigned char)GCL_GetByte(ptr);
        ptr += 1;
        break;

    case GCLCODE_STRING:
        *value_p = gcl_store_ptr(ptr + 1);
        size = *ptr;
        goto ADD_SIZE_80020834;

    case GCLCODE_STACK_VAR:
        *value_p = GCL_GetArgs(*ptr);
        *type_p = 1;
        ptr += 1;
        break;

    case GCLCODE_SCRIPT_DATA:
        *value_p = gcl_store_ptr(ptr + 2);
        size = GCL_GetShort(ptr);
        ptr += size;
        break;

    case GCLCODE_EXPRESSION:
        *value_p = GCL_Expr(ptr + 1, value_p);
        size = *ptr;
        ptr += size;
        break;

    case GCLCODE_PARAMETER:
        *type_p |= *ptr << 16;
        *value_p = gcl_store_ptr(ptr + 2);
        size = ptr[1];
    ADD_SIZE_80020834:
        ptr += size + 1;
        break;

    default:
        printf("GCL:WRONG CODE %x\n", gcl_code);
        break;
    }
    next_str_ptr = ptr;
    return ptr;
}

static void GCL_InitArgStack(void)
{
    argstack_p = argbuffer;
}

int *GCL_SetArgStack(GCL_ARGS *args)
{
    int *sp;
    int *sp2;
    int *argv;
    int  i;
    int *org;

    if (!args)
    {
        return NULL;
    }

    argv = (int *)&args->argv[args->argc - 1];
    i = args->argc;
    org = argstack_p;

    while (i > 0)
    {
        // stack push
        sp = argstack_p;
        *sp = *argv;
        argstack_p = sp + 1;

        argv--;
        i--;
    }

    // stack push
    sp2 = argstack_p;
    *sp2 = args->argc;
    argstack_p = sp2 + 1;

    return org;
}

void GCL_UnsetArgStack(void *stack)
{
    if (stack)
    {
        argstack_p = stack;
    }
}

int GCL_GetArgs(int argno)
{
    return argstack_p[~argno];
}

static void GCL_InitCommandLineBuffer(void)
{
    commandline_p = commandlines;
}

void GCL_SetCommandLine(unsigned char *argtop)
{
    unsigned char **pStackTmp = commandline_p;
    (*pStackTmp) = argtop;
    pStackTmp++;
    commandline_p = pStackTmp;
}

void GCL_UnsetCommandLine(void)
{
    commandline_p--;
}

char *GCL_GetOption(char c)
{
    unsigned char *pScript;
    int            code;
    int            value = 0;  /* must be int to match GCL_GetNextValue's int* param */

    if (!commandline_p) {
        printf("[gcl] WARNING: GCL_GetOption('%c') called with NULL commandline_p\n", c);
        return NULL;
    }
    pScript = *(commandline_p - 1);
    if (!pScript) {
        printf("[gcl] WARNING: GCL_GetOption('%c') script pointer is NULL\n", c);
        return NULL;
    }
    do
    {
        pScript = GCL_GetNextValue(pScript, &code, &value);
        if (!pScript || code == GCLCODE_NULL)
        {
            return NULL;
        }
    } while (!GCL_IsParam(code) || (code >> 16 != (c & 0xff)));

    next_str_ptr = (unsigned char *)gcl_resolve_ptr(value);
    return (char *)gcl_resolve_ptr(value);
}

// might be GCL_NextStr
int GCL_StrToInt(unsigned char *pScript)
{
    int code;
    int value;
    next_str_ptr = GCL_GetNextValue(pScript, &code, &value);
    return value;
}

int GCL_StrToSV(unsigned char *pInScript, SVECTOR *pOut3Words)
{
    int             counter = 0;
    unsigned short *pOutIter = (unsigned short *)pOut3Words;
    unsigned char  *pScript = pInScript;
    if (!pScript) {
        printf("[gcl] WARNING: GCL_StrToSV called with NULL script\n");
        return 0;
    }
    do
    {
        int code;
        int value;
        pScript = GCL_GetNextValue(pScript, &code, &value);
        counter++;
        *pOutIter = value;
        pOutIter++;
    } while (counter < 3);

    next_str_ptr = pScript;
    return 0;
}

char *GCL_ReadString(char *ptr)
{
    int type;
    int value;

    ptr = GCL_GetNextValue(ptr, &type, &value);
    next_str_ptr = ptr;

    if (ptr)
    {
        return (char *)gcl_resolve_ptr(value);
    }
    else
    {
        return NULL;
    }
}

unsigned char *GCL_GetParamResult(void)
{
    if (!next_str_ptr) {
        printf("[gcl] WARNING: GCL_GetParamResult: next_str_ptr is NULL\n");
        return NULL;
    }
    if (!*next_str_ptr || GCL_IsParam(*next_str_ptr))
    {
        return NULL;
    }

    return next_str_ptr;
}

int GCL_GetNextParamValue(void)
{
    return GCL_StrToInt(GCL_GetParamResult());
}

void GCL_ReadParamVector(SVECTOR *pOut3Words)
{
    GCL_StrToSV(GCL_GetParamResult(), pOut3Words);
}

void GCL_DiscardValues(unsigned char *top)
{
    int code;
    int value;

    do
    {
        top = GCL_GetNextValue(top, &code, &value);
    } while (code != GCLCODE_NULL);
}

void GCL_ParseInit(void)
{
    GCL_InitArgStack();
    GCL_InitCommandLineBuffer();
}
