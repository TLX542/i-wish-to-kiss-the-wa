#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include "vm_dbg.h"

#define NOPS (16)

#define OPC(i) ((i)>>12)
#define DR(i) (((i)>>9)&0x7)
#define SR1(i) (((i)>>6)&0x7)
#define SR2(i) ((i)&0x7)
#define FIMM(i) ((i>>5)&01)
#define IMM(i) ((i)&0x1F)
#define SEXTIMM(i) sext(IMM(i),5)
#define FCND(i) (((i)>>9)&0x7)
#define POFF(i) sext((i)&0x3F, 6)
#define POFF9(i) sext((i)&0x1FF, 9)
#define POFF11(i) sext((i)&0x7FF, 11)
#define FL(i) (((i)>>11)&1)
#define BR(i) (((i)>>6)&0x7)
#define TRP(i) ((i)&0xFF)   

//New OS declarations
//  OS Bookkeeping constants 
#define OS_MEM_SIZE 4096    // OS Region size. At the same time, constant free-list starting header 

#define Cur_Proc_ID 0       // id of the current process
#define Proc_Count 1        // total number of processes, including ones that finished executing.
#define OS_STATUS 2         // Bit 0 shows whether the PCB list is full or not

//  Process list and PCB related constants
#define PCB_SIZE 6  // Number of fields in a PCB

#define PID_PCB 0   // holds the pid for a process
#define PC_PCB 1    // value of the program counter for the process
#define BSC_PCB 2   // base value of code section for the process
#define BDC_PCB 3   // bound value of code section for the process
#define BSH_PCB 4   // value of heap section for the process
#define BDH_PCB 5   // holds the bound value of heap section for the process

#define CODE_SIZE 4096
#define HEAP_INIT_SIZE 4096
//New OS declarations


bool running = true;

typedef void (*op_ex_f)(uint16_t i);
typedef void (*trp_ex_f)();

enum { trp_offset = 0x20 };
enum regist { R0 = 0, R1, R2, R3, R4, R5, R6, R7, RPC, RCND, RBSC, RBDC, RBSH, RBDH, RCNT };
enum flags { FP = 1 << 0, FZ = 1 << 1, FN = 1 << 2 };

uint16_t mem[UINT16_MAX] = {0};
uint16_t reg[RCNT] = {0};
uint16_t PC_START = 0x3000;

void initOS();
int createProc(char *fname, char* hname);
void loadProc(uint16_t pid);
static inline void tyld();
uint16_t allocMem(uint16_t size);
int freeMem(uint16_t ptr);
static inline uint16_t mr(uint16_t address);
static inline void mw(uint16_t address, uint16_t val);
static inline void tbrk();
static inline void thalt();
static inline void trap(uint16_t i);

static inline uint16_t sext(uint16_t n, int b) { return ((n>>(b-1))&1) ? (n|(0xFFFF << b)) : n; }
static inline void uf(enum regist r) {
    if (reg[r]==0) reg[RCND] = FZ;
    else if (reg[r]>>15) reg[RCND] = FN;
    else reg[RCND] = FP;
}
static inline void add(uint16_t i)  { reg[DR(i)] = reg[SR1(i)] + (FIMM(i) ? SEXTIMM(i) : reg[SR2(i)]); uf(DR(i)); }
static inline void and(uint16_t i)  { reg[DR(i)] = reg[SR1(i)] & (FIMM(i) ? SEXTIMM(i) : reg[SR2(i)]); uf(DR(i)); }
static inline void ldi(uint16_t i)  { reg[DR(i)] = mr(mr(reg[RPC]+POFF9(i))); uf(DR(i)); }
static inline void not(uint16_t i)  { reg[DR(i)]=~reg[SR1(i)]; uf(DR(i)); }
static inline void br(uint16_t i)   { if (reg[RCND] & FCND(i)) { reg[RPC] += POFF9(i); } }
static inline void jsr(uint16_t i)  { reg[R7] = reg[RPC]; reg[RPC] = (FL(i)) ? reg[RPC] + POFF11(i) : reg[BR(i)]; }
static inline void jmp(uint16_t i)  { reg[RPC] = reg[BR(i)]; }
static inline void ld(uint16_t i)   { reg[DR(i)] = mr(reg[RPC] + POFF9(i)); uf(DR(i)); }
static inline void ldr(uint16_t i)  { reg[DR(i)] = mr(reg[SR1(i)] + POFF(i)); uf(DR(i)); }
static inline void lea(uint16_t i)  { reg[DR(i)] =reg[RPC] + POFF9(i); uf(DR(i)); }
static inline void st(uint16_t i)   { mw(reg[RPC] + POFF9(i), reg[DR(i)]); }
static inline void sti(uint16_t i)  { mw(mr(reg[RPC] + POFF9(i)), reg[DR(i)]); }
static inline void str(uint16_t i)  { mw(reg[SR1(i)] + POFF(i), reg[DR(i)]); }
static inline void rti(uint16_t i) {} // unused
static inline void res(uint16_t i) {} // unused
static inline void tgetc() { reg[R0] = getchar(); }
static inline void tout() { fprintf(stdout, "%c", (char)reg[R0]); }
static inline void tputs() {
    uint16_t *p = mem + reg[R0];
    while(*p) {
        fprintf(stdout, "%c", (char)*p);
        p++;
    }
}
static inline void tin() { reg[R0] = getchar(); fprintf(stdout, "%c", reg[R0]); }
static inline void tputsp() { /* Not Implemented */ }

static inline void tinu16() { fscanf(stdin, "%hu", &reg[R0]); }
static inline void toutu16() { fprintf(stdout, "%hu\n", reg[R0]); }


trp_ex_f trp_ex[10] = { tgetc, tout, tputs, tin, tputsp, thalt, tinu16, toutu16, tyld, tbrk };
static inline void trap(uint16_t i) { trp_ex[TRP(i)-trp_offset](); }
op_ex_f op_ex[NOPS] = { /*0*/ br, add, ld, st, jsr, and, ldr, str, rti, not, ldi, sti, jmp, res, lea, trap };

void ld_img(char *fname, uint16_t offset, uint16_t size) {
    FILE *in = fopen(fname, "rb");
    if (NULL==in) {
        fprintf(stderr, "Cannot open file %s.\n", fname);
        exit(1);    
    }
    uint16_t *p = mem + offset;
    fread(p, sizeof(uint16_t), (size), in);
    fclose(in);
}

void run(char* code, char* heap) {

    while(running) {
        uint16_t i = mr(reg[RPC]++);
        op_ex[OPC(i)](i);
    }
}


// YOUR CODE STARTS HERE

// Internal constants/helpers kept inside the editable region.
#define PAGE_WORDS 2048
#define PAGE_COUNT 32
#define VPN_BITS 5
#define OFFSET_MASK 0x7FF
#define PTE_VALID 0x1
#define PTE_READ 0x2
#define PTE_WRITE 0x4
#define PCB_START 12
#define PCB_FIELDS 3
#define PCB_PID 0
#define PCB_PC 1
#define PCB_PTBR 2
#define VPN_CODE0 6
#define VPN_CODE1 7
#define VPN_HEAP0 8
#define VPN_HEAP1 9
#define PT_REGION_START 4096
#define PT_ENTRIES 32
#define MAX_PROCS (PAGE_WORDS / PT_ENTRIES)
#define RPTBR RBSC
#define PTBR RBSC

static inline uint16_t pcb_addr(uint16_t pid) { return PCB_START + (pid * PCB_FIELDS); }
static inline uint16_t ptbr_of_pid(uint16_t pid) { return PT_REGION_START + (pid * PT_ENTRIES); }
static inline uint16_t pte_get(uint16_t ptbr, uint16_t vpn) { return mem[ptbr + vpn]; }
static inline void pte_set(uint16_t ptbr, uint16_t vpn, uint16_t pte) { mem[ptbr + vpn] = pte; }
static inline uint16_t pte_pfn(uint16_t pte) { return (pte >> 11) & 0x1F; }
static inline uint16_t pte_valid(uint16_t pte) { return pte & PTE_VALID; }
static inline uint16_t pte_readable(uint16_t pte) { return pte & PTE_READ; }
static inline uint16_t pte_writable(uint16_t pte) { return pte & PTE_WRITE; }

static inline uint32_t get_bitmap() {
    return (((uint32_t)mem[3]) << 16) | (uint32_t)mem[4];
}

static inline void set_bitmap(uint32_t bitmap) {
    mem[3] = (uint16_t)((bitmap >> 16) & 0xFFFF);
    mem[4] = (uint16_t)(bitmap & 0xFFFF);
}

static inline uint16_t pfn_is_free(uint16_t pfn) {
    uint32_t bitmap = get_bitmap();
    return (bitmap >> (31 - pfn)) & 1U;
}

static inline void mark_pfn_free(uint16_t pfn, uint16_t is_free) {
    uint32_t bitmap = get_bitmap();
    uint32_t bit = 1U << (31 - pfn);
    if (is_free) bitmap |= bit;
    else bitmap &= ~bit;
    set_bitmap(bitmap);
}

static inline int find_first_free_pfn() {
    for (int pfn = 0; pfn < PAGE_COUNT; ++pfn) {
        if (pfn_is_free((uint16_t)pfn)) return pfn;
    }
    return -1;
}

static inline uint16_t has_any_free_pfn() {
    return get_bitmap() != 0;
}

static inline uint16_t resolve_va(uint16_t va, uint16_t for_write) {
    uint16_t vpn = (va >> 11) & 0x1F;
    uint16_t off = va & OFFSET_MASK;

    if (vpn < 6) {
        fprintf(stdout, "Segmentation fault.\n");
        exit(1);
    }

    uint16_t pte = pte_get(reg[RPTBR], vpn);
    if (!pte_valid(pte)) {
        fprintf(stdout, "Segmentation fault inside free space.\n");
        exit(1);
    }

    if (for_write) {
        if (!pte_writable(pte)) {
            fprintf(stdout, "Cannot write to a read-only page.\n");
            exit(1);
        }
    } else {
        if (!pte_readable(pte)) {
            fprintf(stdout, "Cannot read the page.\n");
            exit(1);
        }
    }

    return (uint16_t)((pte_pfn(pte) << 11) | off);
}

static inline void load_obj_into_two_pages(char *fname, uint16_t pfn0, uint16_t pfn1) {
    FILE *in = fopen(fname, "rb");
    if (NULL == in) {
        fprintf(stderr, "Cannot open file %s.\n", fname);
        exit(1);
    }
    fread(mem + (pfn0 * PAGE_WORDS), sizeof(uint16_t), PAGE_WORDS, in);
    fread(mem + (pfn1 * PAGE_WORDS), sizeof(uint16_t), PAGE_WORDS, in);
    fclose(in);
}

static inline uint16_t allocMem_impl(uint16_t ptbr, uint16_t vpn, uint16_t read, uint16_t write) {
    uint16_t old = pte_get(ptbr, vpn);
    if (pte_valid(old)) return 0;

    int pfn = find_first_free_pfn();
    if (pfn < 0) return 0;

    uint16_t pte = (uint16_t)((((uint16_t)pfn) << 11) |
                              ((write == UINT16_MAX) ? PTE_WRITE : 0) |
                              ((read == UINT16_MAX) ? PTE_READ : 0) |
                              PTE_VALID);
    pte_set(ptbr, vpn, pte);
    mark_pfn_free((uint16_t)pfn, 0);
    return 1;
}

static inline int freeMem_impl(uint16_t vpn, uint16_t ptbr) {
    uint16_t pte = pte_get(ptbr, vpn);
    if (!pte_valid(pte)) return 0;
    mark_pfn_free(pte_pfn(pte), 1);
    pte_set(ptbr, vpn, (uint16_t)(pte & (uint16_t)~PTE_VALID));
    return 1;
}

static inline int next_runnable_pid(uint16_t from_pid) {
    uint16_t nproc = mem[Proc_Count];
    if (nproc == 0) return -1;
    for (uint16_t step = 1; step <= nproc; ++step) {
        uint16_t pid = (uint16_t)((from_pid + step) % nproc);
        uint16_t paddr = pcb_addr(pid);
        if (mem[paddr + PCB_PID] != 0xFFFF) return pid;
    }
    return -1;
}

void initOS() {
    mem[Cur_Proc_ID] = 0xFFFF;
    mem[Proc_Count] = 0;
    mem[OS_STATUS] = 0x0000;
    set_bitmap(0xFFFFFFFF);
    mark_pfn_free(0, 0);
    mark_pfn_free(1, 0);
    mark_pfn_free(2, 0);
    return;
}

// process functions to implement
int createProc(char *fname, char* hname) {
    uint16_t pid = mem[Proc_Count];
    if (pid >= MAX_PROCS || (pcb_addr(pid) + PCB_FIELDS - 1) >= OS_MEM_SIZE) {
        mem[OS_STATUS] |= 0x1;
        fprintf(stdout, "The OS memory region is full. Cannot create a new PCB.\n");
        return 0;
    }

    uint16_t paddr = pcb_addr(pid);
    uint16_t ptbr = ptbr_of_pid(pid);

    mem[paddr + PCB_PID] = pid;
    mem[paddr + PCB_PC] = PC_START;
    mem[paddr + PCB_PTBR] = ptbr;

    if (!allocMem_impl(ptbr, VPN_CODE0, UINT16_MAX, 0) ||
        !allocMem_impl(ptbr, VPN_CODE1, UINT16_MAX, 0)) {
        freeMem_impl(VPN_CODE0, ptbr);
        freeMem_impl(VPN_CODE1, ptbr);
        mem[paddr + PCB_PID] = 0xFFFF;
        fprintf(stdout, "Cannot create code segment.\n");
        return 0;
    }

    if (!allocMem_impl(ptbr, VPN_HEAP0, UINT16_MAX, UINT16_MAX) ||
        !allocMem_impl(ptbr, VPN_HEAP1, UINT16_MAX, UINT16_MAX)) {
        freeMem_impl(VPN_CODE0, ptbr);
        freeMem_impl(VPN_CODE1, ptbr);
        freeMem_impl(VPN_HEAP0, ptbr);
        freeMem_impl(VPN_HEAP1, ptbr);
        mem[paddr + PCB_PID] = 0xFFFF;
        fprintf(stdout, "Cannot create heap segment.\n");
        return 0;
    }

    load_obj_into_two_pages(fname,
                            pte_pfn(pte_get(ptbr, VPN_CODE0)),
                            pte_pfn(pte_get(ptbr, VPN_CODE1)));
    load_obj_into_two_pages(hname,
                            pte_pfn(pte_get(ptbr, VPN_HEAP0)),
                            pte_pfn(pte_get(ptbr, VPN_HEAP1)));

    mem[Proc_Count] = pid + 1;
    if (mem[Proc_Count] >= MAX_PROCS || (pcb_addr(mem[Proc_Count]) + PCB_FIELDS - 1) >= OS_MEM_SIZE) mem[OS_STATUS] |= 0x1;
    else mem[OS_STATUS] &= (uint16_t)~0x1;

    return 1;   
}

void loadProc(uint16_t pid) {
    uint16_t paddr = pcb_addr(pid);
    mem[Cur_Proc_ID] = pid;
    reg[RPC] = mem[paddr + PCB_PC];
    reg[RPTBR] = mem[paddr + PCB_PTBR];
}

int freeMem (uint16_t ptr) {
    (void)ptr;
    return 0;
}

uint16_t allocMem (uint16_t size) {
    (void)size;
    return 0;
} 

// instructions to implement
static inline void tbrk() {
    uint16_t cur = mem[Cur_Proc_ID];
    uint16_t req = reg[R0];
    uint16_t vpn = (req >> 11) & 0x1F;
    uint16_t is_alloc = req & 0x1;
    uint16_t read = (req & 0x2) ? UINT16_MAX : 0;
    uint16_t write = (req & 0x4) ? UINT16_MAX : 0;

    if (vpn < 8) {
        fprintf(stdout, "Cannot allocate/free memory for the reserved segment.\n");
        thalt();
        return;
    }

    if (is_alloc) {
        fprintf(stdout, "Heap increase requested by process %hu.\n", cur);
        uint16_t pte = pte_get(reg[RPTBR], vpn);
        if (pte_valid(pte)) {
            fprintf(stdout, "Cannot allocate memory for page %hu of pid %hu since it is already allocated.\n", vpn, cur);
            return;
        }
        if (!has_any_free_pfn()) {
            fprintf(stdout, "Cannot allocate more space for pid %hu since there is no free page frames.\n", cur);
            return;
        }
        (void)allocMem_impl(reg[RPTBR], vpn, read, write);
    } else {
        fprintf(stdout, "Heap decrease requested by process %hu.\n", cur);
        if (!freeMem_impl(vpn, reg[RPTBR])) {
            fprintf(stdout, "Cannot free memory of page %hu of pid %hu since it is not allocated.\n", vpn, cur);
            return;
        }
    }
}

static inline void tyld() {
    uint16_t old = mem[Cur_Proc_ID];
    int nxt = next_runnable_pid(old);
    if (nxt < 0 || (uint16_t)nxt == old) {
        return;
    }
    uint16_t old_pcb = pcb_addr(old);
    mem[old_pcb + PCB_PC] = reg[RPC];
    mem[old_pcb + PCB_PTBR] = reg[RPTBR];
    loadProc((uint16_t)nxt);
    fprintf(stdout, "We are switching from process %hu to %hu.\n", old, (uint16_t)nxt);
}

// instructions to modify
static inline void thalt() {
    uint16_t cur = mem[Cur_Proc_ID];
    uint16_t paddr = pcb_addr(cur);

    for (uint16_t vpn = 0; vpn < PT_ENTRIES; ++vpn) {
        (void)freeMem_impl(vpn, reg[RPTBR]);
    }
    mem[paddr + PCB_PID] = 0xFFFF;

    int nxt = next_runnable_pid(cur);
    if (nxt < 0) {
        running = false;
        return;
    }
    loadProc((uint16_t)nxt);
} 

static inline uint16_t mr(uint16_t address) {
    return mem[resolve_va(address, 0)];
}

static inline void mw(uint16_t address, uint16_t val) {
    mem[resolve_va(address, 1)] = val;
}

// Keep compatibility with tests that call allocMem/freeMem with multi-arg signatures.
#define allocMem(ptbr, vpn, read, write) allocMem_impl((ptbr), (vpn), (read), (write))
#define freeMem(vpn, ptbr) freeMem_impl((vpn), (ptbr))

// YOUR CODE ENDS HERE
