/*
 * This file is part of Astra. For more information, visit
 * https://cesbo.com
 *
 * Copyright (C) 2011-2014, Andrey Dyldin <and@cesbo.com>
 */

#if defined(__i386__) || defined(__x86_64__)

#include <stdio.h>
#include <stdlib.h>

#define GCC_VERSION (__GNUC__ * 10000 + (__GNUC_MINOR__ * 100) + __GNUC_PATCHLEVEL__)

typedef struct
{
    const char *arch;
    int curr[2]; // cpu { family, model }
    int next[2]; // check next { family, model } if arch==NULL or gcc not support curr
} cpu_arch_t;

static cpu_arch_t intel_list[] =
{ /* from the begin to the end */
    // Haswell
    { "corei7-avx2", {6,60}, {6,42} }
    // Ivy Bridge
    , { "corei7-avx-i", {6,58}, {6,42} }
    // Sandy Bridge
    , { "corei7-avx", {6,42}, {6,15} }
    // Core i7,i5,i3, Xeon 55xx
        , { NULL, {6,44}, {6,26} }, { NULL, {6,37}, {6,26} }, { NULL, {6,46}, {6,26} }
        , { NULL, {6,47}, {6,26} }, { NULL, {6,29}, {6,26} }, { NULL, {6,30}, {6,26} }
    , { "corei7", {6,26}, {6,15} }
    // Atom
    , { "atom", {6,28}, {6,15} }
    // Pentium 4, Pentium D, Celeron D
        , { NULL, {15,4}, {15,3} }, { NULL, {15,6}, {15,3} }
#if defined(__i386__)
    , { "prescott", {15,3}, {6,9} }
#elif defined(__x86_64__)
    , { "nocona", {15,3}, {6,9} }
#endif
    // Pentium 4
        , { NULL, {15,1}, {15,0} }, { NULL, {15,2}, {15,0} }
    , { "pentium4", {15,0}, {5,1} }
    // Core 2 Duo/Quad, Xeon 51xx/53xx/54xx/3360
        , { NULL, {6,23}, {6,15} }
    , { "core2", {6,15}, {6,14} }
    // Core Solo/Duo
        , { NULL, {6,22}, {6,14} }
    , { "prescott", {6,14}, {6,9} }
        , { NULL, {6,13}, {6,9} }
    , { "pentium-m", {6,9}, {6,7} }
        , { NULL, {6,11}, {6,7} }, { NULL, {6,10}, {6,7} }, { NULL, {6,8}, {6,7} }
    , { "pentium3", {6,7}, {6,1} }
        , { NULL, {6,5}, {6,3} }, { NULL, {6,6}, {6,3} }
    , { "pentium2", {6,3}, {6,1} }
    , { "pentiumpro", {6,1}, {5,5} }
    , { "pentium-mmx", {5,4}, {5,1} }
        , { NULL, {5,2}, {5,1} }, { NULL, {5,3}, {5,1} }
    , { "pentium", {5,1}, {0,0} }
    , { "i486", {4,0}, {0,0} }
    , { "i386", {3,0}, {0,0} }
    , { NULL, {0,0}, {0,0} }
};

static cpu_arch_t amd_list[] =
{
      { "k6-2", {5,8}, {0,0} } // my first x86 pc :)
    , { NULL, {0,0}, {0,0} }
};

const char * cpu_arch_get(cpu_arch_t *list, int family, int model)
{
    char cmd[64];
    while(list->curr[0])
    {
        if(list->curr[0] == family && list->curr[1] == model)
        {
            sprintf(cmd, "gcc -march=%s -E -xc /dev/null 1>/dev/null 2>/dev/null", list->arch);
            if(system(cmd) == 0)
                return list->arch;
            else
                return cpu_arch_get(list, list->next[0], list->next[1]);
        }
        ++list;
    }
    return NULL;
}

typedef struct { unsigned int eax, ebx, ecx, edx; } regs_t;
static inline void cpuid(regs_t *regs, unsigned int op)
{
    __asm__ __volatile__ (  "cpuid"
                          : "=a" (regs->eax)
                          , "=b" (regs->ebx)
                          , "=c" (regs->ecx)
                          , "=d" (regs->edx)
                          : "a"  (op));
}

int main()
{
    regs_t vr, ir;
    cpuid(&vr, 0);
    cpuid(&ir, 1);

    // TODO: get CPU cores -DCPU_CORES=%d
//    unsigned int logical = (ir.ebx >> 24) & 0xff;
//    printf("test:%d\n", logical);

    int cpu_family = (ir.eax & 0x00000F00) >> 8;
    int cpu_model = (ir.eax & 0x000000F0) >> 4;
    if(vr.ebx == 0x756e6547
       && vr.edx == 0x49656e69
       && vr.ecx == 0x6c65746e)
    {
        /* GenuineIntel */
        cpu_family += ((ir.eax & 0x0FF00000) >> 20);
        cpu_model += ((ir.eax & 0x000F0000) >> 12 /* (>>16)<<4 */ );
        const char *march = cpu_arch_get(intel_list, cpu_family, cpu_model);
        if(march)
            printf(" -march=%s", march);
    }
    else if(vr.ebx == 0x68747541
            && vr.edx == 0x69746E65
            && vr.ecx == 0x444D4163)
    {
        /* AuthenticAMD */
        if(cpu_family == 0xF)
        {
            cpu_family += ((ir.eax & 0x0FF00000) >> 20);
            cpu_model += ((ir.eax & 0x000F0000) >> 12 /* (>>16)<<4 */ );
        }
        const char *march = cpu_arch_get(amd_list, cpu_family, cpu_model);
        if(march)
            printf(" -march=%s", march);
    }

    if(ir.ecx & (0x00080000 /* 4.1 */ | 0x00100000 /* 4.2 */ ))
        printf(" -msse4");
    else if(ir.ecx & 0x00000001)
        printf(" -msse3");
    else if(ir.edx & 0x04000000)
        printf(" -msse2");
    else if(ir.edx & 0x02000000)
        printf(" -msse");
    else if(ir.edx & 0x00800000)
        printf(" -mmmx");
    putchar('\n');

    return 0;
}

#else

int main() { return 0; }

#endif
