void OPPROTO glue(glue(op_ldub, MEMSUFFIX), _T0_A0)(void)
{
    T0 = glue(ldub, MEMSUFFIX)(A0);
}

void OPPROTO glue(glue(op_ldsb, MEMSUFFIX), _T0_A0)(void)
{
    T0 = glue(ldsb, MEMSUFFIX)(A0);
}

void OPPROTO glue(glue(op_lduw, MEMSUFFIX), _T0_A0)(void)
{
    T0 = glue(lduw, MEMSUFFIX)(A0);
}

void OPPROTO glue(glue(op_ldsw, MEMSUFFIX), _T0_A0)(void)
{
    T0 = glue(ldsw, MEMSUFFIX)(A0);
}

void OPPROTO glue(glue(op_ldl, MEMSUFFIX), _T0_A0)(void)
{
    T0 = (uint32_t)glue(ldl, MEMSUFFIX)(A0);
}

void OPPROTO glue(glue(op_ldub, MEMSUFFIX), _T1_A0)(void)
{
    T1 = glue(ldub, MEMSUFFIX)(A0);
}

void OPPROTO glue(glue(op_ldsb, MEMSUFFIX), _T1_A0)(void)
{
    T1 = glue(ldsb, MEMSUFFIX)(A0);
}

void OPPROTO glue(glue(op_lduw, MEMSUFFIX), _T1_A0)(void)
{
    T1 = glue(lduw, MEMSUFFIX)(A0);
}

void OPPROTO glue(glue(op_ldsw, MEMSUFFIX), _T1_A0)(void)
{
    T1 = glue(ldsw, MEMSUFFIX)(A0);
}

void OPPROTO glue(glue(op_ldl, MEMSUFFIX), _T1_A0)(void)
{
    T1 = glue(ldl, MEMSUFFIX)(A0);
}

void OPPROTO glue(glue(op_stb, MEMSUFFIX), _T0_A0)(void)
{
    glue(stb, MEMSUFFIX)(A0, T0);
}

void OPPROTO glue(glue(op_stw, MEMSUFFIX), _T0_A0)(void)
{
    glue(stw, MEMSUFFIX)(A0, T0);
}

void OPPROTO glue(glue(op_stl, MEMSUFFIX), _T0_A0)(void)
{
    glue(stl, MEMSUFFIX)(A0, T0);
}

#if 0
void OPPROTO glue(glue(op_stb, MEMSUFFIX), _T1_A0)(void)
{
    glue(stb, MEMSUFFIX)(A0, T1);
}
#endif

void OPPROTO glue(glue(op_stw, MEMSUFFIX), _T1_A0)(void)
{
    glue(stw, MEMSUFFIX)(A0, T1);
}

void OPPROTO glue(glue(op_stl, MEMSUFFIX), _T1_A0)(void)
{
    glue(stl, MEMSUFFIX)(A0, T1);
}

/* SSE support */
void OPPROTO glue(glue(op_ldo, MEMSUFFIX), _env_A0)(void)
{
    XMMReg *p;
    p = (XMMReg *)((char *)env + PARAM1);
    /* XXX: host endianness ? */
    p->u.q[0] = glue(ldq, MEMSUFFIX)(A0);
    p->u.q[1] = glue(ldq, MEMSUFFIX)(A0 + 8);
}

void OPPROTO glue(glue(op_sto, MEMSUFFIX), _env_A0)(void)
{
    XMMReg *p;
    p = (XMMReg *)((char *)env + PARAM1);
    /* XXX: host endianness ? */
    glue(stq, MEMSUFFIX)(A0, p->u.q[0]);
    glue(stq, MEMSUFFIX)(A0 + 8, p->u.q[1]);
}

#ifdef TARGET_X86_64
void OPPROTO glue(glue(op_ldsl, MEMSUFFIX), _T0_A0)(void)
{
    T0 = (int32_t)glue(ldl, MEMSUFFIX)(A0);
}

void OPPROTO glue(glue(op_ldsl, MEMSUFFIX), _T1_A0)(void)
{
    T1 = (int32_t)glue(ldl, MEMSUFFIX)(A0);
}

void OPPROTO glue(glue(op_ldq, MEMSUFFIX), _T0_A0)(void)
{
    T0 = glue(ldq, MEMSUFFIX)(A0);
}

void OPPROTO glue(glue(op_ldq, MEMSUFFIX), _T1_A0)(void)
{
    T1 = glue(ldq, MEMSUFFIX)(A0);
}

void OPPROTO glue(glue(op_stq, MEMSUFFIX), _T0_A0)(void)
{
    glue(stq, MEMSUFFIX)(A0, T0);
}

void OPPROTO glue(glue(op_stq, MEMSUFFIX), _T1_A0)(void)
{
    glue(stq, MEMSUFFIX)(A0, T1);
}
#endif

#undef MEMSUFFIX
