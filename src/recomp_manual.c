/**
 * Manual function overrides and ICALL diagnostics
 *
 * This file provides:
 *   - recomp_lookup_manual()  : intercept specific Xbox VAs with hand-written code
 *   - recomp_icall_fail_log() : log when an indirect call target can't be resolved
 *   - ICALL trace ring buffer  : globals used by the RECOMP_ICALL macro
 *
 * The recomp pipeline generates an auto-dispatch table (recomp_lookup) that
 * resolves most function addresses. recomp_lookup_manual() is called FIRST,
 * giving you a chance to override any function with a custom implementation.
 *
 * Common reasons to add manual overrides:
 *   - Trace a function to understand call flow (wrap the generated version)
 *   - Fix a function the lifter translated incorrectly
 *   - Stub out a function that crashes (return early, set eax to a safe value)
 *   - Redirect a function to a native implementation (e.g., skip CRT init)
 *   - Intercept D3D/audio calls for custom rendering or sound
 */

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

/* The generated register model (g_eax/g_esp are thread-local there) and the
 * XBOX_PTR/MEM32 accessors; the register names below are its macros. */
#define RECOMP_GENERATED_CODE
#include "recomp_funcs.h"

/* ── ICALL trace ring buffer ───────────────────────────────── */

/*
 * These globals are written by the RECOMP_ICALL macro (defined in
 * recomp_types.h) every time an indirect call is dispatched. When a
 * crash occurs, the VEH handler or recomp_icall_fail_log() can dump
 * the last 16 call targets to help you trace what happened.
 *
 * The runtime owns them: xbox_kernel defines all three in
 * src/kernel/xbox_memory_layout.c, and recomp_types.h declares them extern.
 * Declare, do not define -- a definition here as well is a duplicate symbol,
 * and a project copied from this template failed to link on all three:
 *
 *   xbox_memory_layout.obj : error LNK2005: g_icall_count already defined
 *                            in recomp_manual.obj
 */
extern volatile uint32_t g_icall_trace[16];
extern volatile uint32_t g_icall_trace_idx;
extern volatile uint64_t g_icall_count;

typedef void (*recomp_func_t)(void);

/* ── Register state (defined in xbox_memory_layout.c) ──────── */

extern ptrdiff_t g_xbox_mem_offset;

/* ── CRT memmove/memcpy (0x002016B0) ──────────────────────────
 *
 * The MSVC CRT memmove (memcpy is the same body). Its tail and backward-copy
 * paths dispatch through UnwindDown-style vectors indexed with a *negative*
 * register -- the table address in the instruction is the end of the table --
 * which the lifter's forward table reader cannot follow, so those jumps are
 * lowered to indirect tail calls into the middle of the function and fail to
 * resolve: every copy whose length is not a multiple of 4, and every
 * overlapping backward copy, would silently lose its tail (found in NFSU2,
 * same LIBCMT 1.0.5849).
 *
 * Replaced by the host memmove. cdecl (dst, src, count), returns dst, and
 * esi/edi are preserved, as the original restores them. Generated bodies are
 * skipped by tools.recomp --exclude-manual reading this file. */
static void crt_memmove(void)
{
    uint32_t dst = MEM32(esp + 4);
    uint32_t src = MEM32(esp + 8);
    uint32_t n   = MEM32(esp + 12);

    if (n)
        memmove((void *)XBOX_PTR(dst), (const void *)XBOX_PTR(src), n);
    eax = dst;
    esp += 4;   /* return address; cdecl, the caller pops the arguments */
}

void sub_002016B0(void) { crt_memmove(); }

/* ── D3DDevice_SetTimerCallback (0x00216410) ─────────────────
 *
 * stdcall (ULONGLONG time, callback, context), ret 0x10: calls back at rdtsc
 * time `time`, from D3D's DPC when the PTIMER alarm (now emulated in the
 * runtime) goes off. A time already past -- within 0x28 ticks of now --
 * is refused with 0x8876082A and nothing is armed.
 *
 * The game paces its 60 Hz input frames with one (0x000D7DE0 re-arms from
 * the callback 0x000D7EA0 at "expected + period"), and the game logic runs
 * once per input frame. Here the DPC waits for the guest lock, and during
 * loads a callback came more than a period late: the re-arm was in the past,
 * refused, and the chain was gone for good -- the game rendered its loading
 * screen forever after 9 frames. A late time is moved to just after now, so
 * the callback runs at once and the game catches up through its input ring. */
extern uint64_t xbox_ReadTimeStampCounter(void);
extern void sub_00216410_gen(void);

void sub_00216410(void)
{
    uint64_t want = (uint64_t)MEM32(esp + 4) | ((uint64_t)MEM32(esp + 8) << 32);
    /* 1 ms ahead: D3D reads the TSC again after this, and the guest lock
     * can change hands in between -- a 5.6 us margin was still refused. */
    uint64_t soon = xbox_ReadTimeStampCounter() + 733333;

    static int trace = -1;
    static unsigned n;
    uint32_t cb = MEM32(esp + 12);

    if (trace < 0)
        trace = getenv("DEFJAM_TIMER_TRACE") && *getenv("DEFJAM_TIMER_TRACE") == '1';
    if (cb && want < soon) {
        MEM32(esp + 4) = (uint32_t)soon;
        MEM32(esp + 8) = (uint32_t)(soon >> 32);
    }
    sub_00216410_gen();
    if (trace && n++ < 200)
        fprintf(stderr, "[timer] SetTimerCallback(%llu ticks %s, cb %08X) -> %08X\n",
                (unsigned long long)(want - (soon - 733333)),
                want < soon ? "LATE" : "ahead", cb, eax);
}

/* ── D3D PTIMER handler (0x002232E0) ─────────────────────────
 *
 * thiscall, ecx = the device's hardware block. Called by D3D's interrupt
 * dispatcher (0x002239A0) when PMC_INTR_0 bit 20 is up: acknowledges
 * PTIMER_INTR_0 (write-1-to-clear; nothing on RAM) and runs the callbacks
 * whose time has come, then re-programs the alarm. The runtime raises the
 * alarm (xbox_memory_layout.c, PTIMER); tell it the alarm was taken, or the
 * bits stay up and D3D's busy-waits, which call the dispatcher themselves,
 * run the handler -- and the game's input-frame callback -- again for every
 * turn. DEFJAM_TIMER_TRACE=1 logs the slots before and after. */
extern void sub_002232E0_gen(void);
extern void xbox_Nv2aPtimerTaken(void);

void sub_002232E0(void)
{
    static int trace = -1;
    static unsigned n;
    uint32_t blk = ecx;

    if (trace < 0)
        trace = getenv("DEFJAM_TIMER_TRACE") && *getenv("DEFJAM_TIMER_TRACE") == '1';
    if (trace && n < 200)
        fprintf(stderr, "[timer] PTIMER handler in: slots %08X:%08X %08X:%08X cur %u armed %u "
                "time %08X:%08X alarm %08X\n",
                MEM32(blk + 0x834), MEM32(blk + 0x830), MEM32(blk + 0x83C), MEM32(blk + 0x838),
                MEM32(blk + 0x840), MEM32(blk + 0x818),
                MEM32(0xFD009410u), MEM32(0xFD009400u), MEM32(0xFD009420u));
    sub_002232E0_gen();
    xbox_Nv2aPtimerTaken();
    if (trace && n++ < 200)
        fprintf(stderr, "[timer] PTIMER handler out: slots %08X:%08X %08X:%08X cur %u armed %u "
                "alarm %08X en %u\n",
                MEM32(blk + 0x834), MEM32(blk + 0x830), MEM32(blk + 0x83C), MEM32(blk + 0x838),
                MEM32(blk + 0x840), MEM32(blk + 0x818), MEM32(0xFD009420u), MEM32(0xFD009140u));
}

/* ── D3D fence wait, D3D_BlockOnTime (0x0021EC50) ────────────
 *
 * stdcall (fence, flags), ret 8. The XDK 5849 D3D keeps its device behind
 * the global at 0x00234768:
 *   +0x2C  write sequence, bumped as fences are inserted
 *   +0x30  pointer to the semaphore the GPU releases as it completes work
 * and this spins (0x0021ED87) until *(+0x30) catches up with the fence it
 * was asked for. The pushbuffer executor (RECOMP_PB_EXEC) only releases a
 * semaphore it has been told about, so hand it the real one once and run
 * the original wait against it -- reporting the GPU caught up instead would
 * let D3D reuse ring space and vertex memory the executor has not read yet
 * (the toolkit's d3d8ltcg-device-context.md). Same as NFSU2's 0x002E8F20;
 * without it the boot hung here before the first frame. */
#define DEFJAM_D3D_DEVICE_PTR 0x00234768u
extern void nv2a_pb_set_semaphore_target(uint32_t guest_va);
extern void sub_0021EC50_gen(void);

void sub_0021EC50(void)
{
    static int exec = -1;
    uint32_t dev = MEM32(DEFJAM_D3D_DEVICE_PTR);
    uint32_t sem = dev ? MEM32(dev + 0x30) : 0;

    if (exec < 0)
        exec = getenv("RECOMP_PB_EXEC") != NULL;
    if (exec) {
        static uint32_t registered;
        if (sem && sem != registered) {
            nv2a_pb_set_semaphore_target(sem);
            fprintf(stderr, "[D3D] GPU semaphore at 0x%08X\n", sem);
            registered = sem;
        }
        sub_0021EC50_gen();
        return;
    }
    if (sem)
        MEM32(sem) = MEM32(dev + 0x2C);     /* "GPU" is always caught up */
    eax = 0;
    esp += 4 + 8;                           /* return address, 2 stdcall args */
}

/* ── Front-buffer copy (0x0010D690) ──────────────────────────
 *
 * stdcall(dst): GetBackBuffer(-1) -- the front buffer --, GetDesc, LockRect
 * (0x0021DCB0), memcpy of width*height*4 bytes to dst, UnlockRect. The end of
 * the Sketch Artist uses it to keep the finished sketch as the next screen's
 * backdrop. The picture is in the renderer, not in guest memory, so the copy
 * was black; while this function runs, LockRect has the renderer write the
 * screen into the locked bits first. */
extern void sub_0010D690_gen(void);
extern void sub_0021DCB0_gen(void);
extern int (*xbox_nv2a_read_front)(uint32_t va, uint32_t pitch, uint32_t rows);
static int s_front_copy;

void sub_0010D690(void)
{
    s_front_copy = 1;
    sub_0010D690_gen();
    s_front_copy = 0;
}

/* LockRect(surface, D3DLOCKED_RECT *out {Pitch, pBits}, rect, flags), stdcall. */
void sub_0021DCB0(void)
{
    uint32_t out = MEM32(esp + 8), ret = MEM32(esp), surf = MEM32(esp + 4);
    static int trace = -1;

    sub_0021DCB0_gen();
    if (trace < 0)
        trace = getenv("DEFJAM_LOCK_TRACE") && *getenv("DEFJAM_LOCK_TRACE") == '1';
    if (trace && out)
        fprintf(stderr, "[lock] LockRect(surface %08X) from %08X -> bits %08X pitch %u%s\n",
                surf, ret, MEM32(out + 4), MEM32(out), s_front_copy ? " (front-buffer copy)" : "");
    if (s_front_copy && out && xbox_nv2a_read_front) {
        uint32_t pitch = MEM32(out), bits = MEM32(out + 4);
        if (pitch && bits)
            xbox_nv2a_read_front(bits, pitch, 4096);
    }
}

/* ── D3D interrupt handlers at DISPATCH_LEVEL ────────────────
 *
 * D3D services the GPU from its DPC and, with the interrupt
 * masked, from its own busy-waits (BlockUntilIdle and friends) on the game
 * thread: both call the vblank handler (0x002231D0) and the PGRAPH handler
 * (0x00223760), which queues flips. On the Xbox the two never interleave --
 * one is a DPC, the other runs with the interrupt off. Here the DPC runs on
 * the kernel timer thread, and lifted code hands over the guest lock at any
 * function entry, so the vblank count could move between D3D computing a
 * flip's target vblank and checking it. D3D retires a flip only when the
 * count *equals* its target, so that flip stayed pending forever and
 * PersistDisplay, which waits for pending flips, never returned (NFSU2: the
 * hang starting a race). Run at DISPATCH_LEVEL (the dispatch lock,
 * kernel_hal.c), neither can be interrupted by the other.
 *
 * Raising blocks on the lock that the timer thread holds while it waits for
 * the guest lock, so the guest lock is let go for the raise, as a kernel
 * call would; already at DISPATCH (inside the DPC) there is nothing to take. */
extern uint8_t xbox_KfRaiseIrql(uint8_t irql);
extern void xbox_KfLowerIrql(uint8_t irql);
extern uint8_t xbox_CurrentIrql(void);
extern int xbox_gil_suspend(void);
extern void xbox_gil_resume(int depth);
extern void xbox_Nv2aVblankTaken(void);

static uint8_t d3d_isr_enter(void)
{
    uint8_t old = xbox_CurrentIrql();

    if (old < 2) {
        int d = xbox_gil_suspend();
        old = xbox_KfRaiseIrql(2);
        xbox_gil_resume(d);
    }
    return old;
}

static void d3d_isr_leave(uint8_t old)
{
    if (old < 2)
        xbox_KfLowerIrql(old);
}

/* Flips retired by a handler call: D3D's retire index, at +0x1BC of the
 * block both handlers take in ecx (their own reads index the pending-flip
 * slots from it). The pushbuffer executor's FLIP_STALL waits on these, as
 * PGRAPH waits on the retire's PGRAPH_INCREMENT write. */
extern void nv2a_pb_flip_retired(unsigned n);

/* Vblank handler, thiscall. It ends by writing PCRTC_INTR and spinning until
 * PMC_INTR bit 24 drops. Its callers (the DPC, D3D's busy-waits) have read
 * the bits before calling it, so this is the vblank being taken: clear them
 * now, and the spin ends at once instead of holding the guest lock until the
 * runtime's ack thread comes round. */
extern void sub_002231D0_gen(void);

void sub_002231D0(void)
{
    uint32_t blk = ecx;
    /* Read only once at DISPATCH: read before, a retire by the other
     * handler (DPC thread vs a D3D busy-wait) in between was counted by
     * both, the executor's flip index ran one off for good, and every
     * FLIP_STALL then waited out its 250 ms (NFS Carbon, console, 3.7 fps). */
    uint8_t old = d3d_isr_enter();
    uint32_t head = MEM32(blk + 0x1BC);

    xbox_Nv2aVblankTaken();
    sub_002231D0_gen();
    nv2a_pb_flip_retired(MEM32(blk + 0x1BC) - head);
    d3d_isr_leave(old);
}

/* PGRAPH handler, thiscall, ecx = the device's hardware block (+0 =
 * 0xFD000000). Reads a software-method trap from PGRAPH_INTR / NSOURCE /
 * TRAPPED_ADDR / TRAPPED_DATA, acknowledges it by writing PGRAPH_INTR back,
 * and acts on it: NOP(5) sets the event BlockOnTime sleeps on, a flip trap
 * queues the flip. The acknowledge is write-1-to-clear on
 * hardware and a no-op on RAM. Left pending, every turn of a busy-wait took
 * the same trap again (tens of thousands of flips a second, until the flip
 * targets were millions of vblanks ahead); guessed from the handler's
 * PGRAPH_FIFO writes, a trap posted during a call was sometimes dropped (the
 * fence event never set). So the executor posts traps under a lock held here
 * around the call, and a call that found one reports it taken, which clears
 * it (nv2a_pb_exec.c). */
extern void sub_00223760_gen(void);
extern void nv2a_pb_trap_lock(void);
extern void nv2a_pb_trap_unlock(void);
extern void nv2a_pb_trap_taken(void);

void sub_00223760(void)
{
    volatile uint32_t *nv = (volatile uint32_t *)XBOX_PTR(0xFD000000u);
    uint32_t blk = ecx;
    uint8_t old = d3d_isr_enter();
    uint32_t head = MEM32(blk + 0x1BC);     /* at DISPATCH, as above */
    uint32_t nsource;

    nv2a_pb_trap_lock();
    nsource = nv[0x400108 / 4];
    sub_00223760_gen();
    nv2a_pb_flip_retired(MEM32(blk + 0x1BC) - head);   /* a flip queued on its vblank retires at once */
    if (nsource)
        nv2a_pb_trap_taken();
    nv2a_pb_trap_unlock();
    d3d_isr_leave(old);
}

/* ── DirectSound DSP command post (0x0025F394) ───────────────
 *
 * thiscall, ecx = the DSP-side object. Copies a command block into the GP
 * DSP's scratch area, stores the command code at scratch + 0x810, and spins
 * until the DSP zeroes it (this loop does re-read memory). The scratch block
 * is ***(this + 8 + 0x10), the same chain the runtime's RECOMP_DSP_ACK notes
 * describe. There is no DSP, so register the word with the runtime's
 * completion list before running the original body. */
extern int xbox_ApuDspAckWord(uint32_t va);
extern void sub_0025F394_gen(void);

void sub_0025F394(void)
{
    uint32_t obj = MEM32(ecx + 8);
    uint32_t blk = obj ? MEM32(obj + 0x10) : 0;
    uint32_t scratch = blk ? MEM32(blk) : 0;

    if (scratch)
        xbox_ApuDspAckWord(scratch + 0x810);
    sub_0025F394_gen();
}

/* ── DirectSound AC'97 channel reset (0x00267B3D) ────────────
 *
 * thiscall, ecx = channel object. Sets RR (bit 1) in the channel's NABM
 * control byte, then waits for the controller to clear it -- except MSVC
 * hoisted the load out of the loop, so the original reads the byte once and
 * spins on the register copy forever unless the bit is already clear at that
 * one read:
 *
 *     mov  byte [eax+0xFEC0010B], 2
 *     mov  cl, [eax+0xFEC0010B]
 *     and  cl, 2
 *   L: test cl, cl
 *     jne  L
 *
 * On hardware the reset has completed by then. There is no fault handling on
 * POSIX or Switch, so this is the same function with the reset completing at
 * once. Everything else -- the lock taken through sub_00256A8C/sub_00256AAE,
 * the buffer-descriptor base store, the 0xFEC0017C write for channel 1 and
 * the sub_00267880 re-arm -- is as the original does it. */
void sub_00267B3D(void)
{
    uint32_t ebp_saved = g_ebp, frame, chan, nabm;

    PUSH32(esp, ebp_saved);
    frame = esp;
    g_ebp = frame;
    g_seh_ebp = frame;
    esp -= 8;
    MEM32(frame - 4) = 0;
    PUSH32(esp, esi);
    esi = ecx;
    chan = esi;

    ecx = frame - 8;
    PUSH32(esp, 0x00267B51u); RECOMP_ABI_CALL(0x00256A8Cu, sub_00256A8C);

    nabm = MEM32(MEM32(chan) * 4 + 0x27C4C8u);
    MEM8(nabm + 0xFEC0010Bu) = 2;
    MEM8(nabm + 0xFEC0010Bu) &= (uint8_t)~2u;   /* reset complete */
    eax = nabm;
    SET_LO8(ecx, 0);

    MEM32(nabm + 0xFEC00100u) = MEM32(chan + 0x1C);
    if (MEM32(chan) == 1)
        MEM32(0xFEC0017Cu) = MEM32(chan + 0x28);

    PUSH32(esp, 1);
    PUSH32(esp, 1);
    ecx = chan;
    PUSH32(esp, MEM8(chan + 0x24));
    PUSH32(esp, MEM8(chan + 0x25));
    eax = MEM8(chan + 0x25);
    g_ebp = frame;
    g_seh_ebp = frame;
    PUSH32(esp, 0x00267BA4u); RECOMP_ABI_CALL(0x00267880u, sub_00267880);

    ecx = frame - 8;
    g_ebp = frame;
    g_seh_ebp = frame;
    PUSH32(esp, 0x00267BACu); RECOMP_ABI_CALL(0x00256AAEu, sub_00256AAE);

    POP32(esp, esi);
    esp = frame;
    POP32(esp, ebp_saved);
    g_ebp = ebp_saved;
    esp += 4;
}

/* ── Manual function overrides ─────────────────────────────── */

/*
 * Return a function pointer to override the given Xbox VA, or NULL
 * to fall through to the auto-generated dispatch table.
 *
 * This is called on every indirect call (RECOMP_ICALL) and every
 * direct call through the dispatch table, so keep it fast. A chain
 * of if-statements on uint32_t compiles to a simple comparison
 * sequence; for large override tables, consider a sorted array
 * with binary search.
 *
 * Examples of common override patterns:
 *
 *   // Trace wrapper: log entry/exit around the generated function
 *   extern void sub_00012345(void);
 *   static void traced_sub_00012345(void) {
 *       fprintf(stderr, "[TRACE] sub_00012345 entered, eax=0x%08X\n", g_eax);
 *       sub_00012345();
 *       fprintf(stderr, "[TRACE] sub_00012345 returned, eax=0x%08X\n", g_eax);
 *   }
 *
 *   // Stub: skip a function entirely (return 0 in eax)
 *   static void stub_00067890(void) {
 *       g_eax = 0;
 *   }
 *
 *   // Fix: replace a broken lifted function with correct C
 *   static void fixed_sub_000ABCDE(void) {
 *       // Read arguments from stack/registers per calling convention
 *       uint32_t arg1 = g_ecx;
 *       uint32_t arg2 = MEM32(g_esp + 4);
 *       // ... correct implementation ...
 *       g_eax = result;
 *   }
 */
recomp_func_t recomp_lookup_manual(uint32_t xbox_va)
{
    /*
     * TODO: Add your overrides here. Examples:
     *
     * if (xbox_va == 0x00012345) return traced_sub_00012345;
     * if (xbox_va == 0x00067890) return stub_00067890;
     * if (xbox_va == 0x000ABCDE) return fixed_sub_000ABCDE;
     */

    (void)xbox_va;
    return (recomp_func_t)0;
}

/* ── ICALL failure logging ─────────────────────────────────── */

/*
 * Called when RECOMP_ICALL cannot resolve a target address.
 * This usually means one of:
 *   - A vtable dispatch to an address not in the dispatch table
 *   - A function pointer loaded from uninitialized or corrupt memory
 *   - A kernel thunk address that the bridge doesn't handle
 *
 * During early bring-up you will see many of these. Most are harmless
 * (the ICALL macro pops the dummy return address and continues).
 * Focus on the ones that cause crashes or incorrect behavior.
 */
void recomp_icall_fail_log(uint32_t va)
{
    /* The ICALL failure path stores esp through first (RECOMP_ICALL_FAIL_SYNC),
     * so the pushed return address names the call site. */
    fprintf(stderr, "[ICALL] Failed to resolve VA 0x%08X (total calls: %llu, "
            "called from 0x%08X)\n",
            va, (unsigned long long)g_icall_count, MEM32(g_esp));

    /* Dump last 16 call targets from the ring buffer */
    fprintf(stderr, "  Recent ICALL targets:\n");
    for (int i = 0; i < 16; i++) {
        int idx = (g_icall_trace_idx - 16 + i) & 15;
        if (g_icall_trace[idx])
            fprintf(stderr, "    [%2d] 0x%08X\n", i, g_icall_trace[idx]);
    }
    fflush(stderr);
}

/* An indirect call whose target is not code: a null or wild function pointer.
 *
 * Skipping these is right -- calling a data address is worse -- but skipping
 * them *silently* is not. They almost always arrive inside a loop, so the
 * symptom is a hang with no output rather than a diagnosable null vtable call.
 *
 * Rate-limited per address: a spin can produce millions of these, and the
 * useful information is which addresses occur, not how often.
 */
void recomp_icall_not_code_log(uint32_t va)
{
    enum { SLOTS = 16 };
    static uint32_t seen[SLOTS];
    static uint64_t hits[SLOTS];
    static int count;
    int i;

    for (i = 0; i < count; i++)
        if (seen[i] == va)
            break;
    if (i == count) {
        if (count == SLOTS)
            return;
        seen[count] = va;
        hits[count] = 0;
        count++;
    }
    hits[i]++;
    /* Report at 1, 10, 100, 1000 ... rather than once. A single line says a
     * wild pointer was skipped; the progression says it is being skipped in a
     * loop, which is the difference between a curiosity and the reason the
     * title is hung. */
    {
        uint64_t n = hits[i];
        while (n >= 10 && n % 10 == 0)
            n /= 10;
        if (n != 1)
            return;
    }
    fprintf(stderr, "[ICALL] target 0x%08X is not code -- skipped %llu time(s) "
                    "(null or wild function pointer, at call #%llu, from 0x%08X)\n",
            va, (unsigned long long)hits[i],
            (unsigned long long)g_icall_count, MEM32(g_esp));
    fflush(stderr);
}

/* ── Untranslated instructions ───────────────────────────────────────────
 *
 * The lifter emits RECOMP_UNIMPL(text, va) at every instruction it has no
 * translation for, in place of the bare comment it used to leave. The
 * instruction is still a no-op; this only stops the omission being silent.
 * RECOMP_UNIMPL_TRAP=1 aborts at the first hit, at the guest address of the
 * cause rather than wherever the damage surfaces. */
#include <stdlib.h>

void recomp_unimpl(const char *text, uint32_t va)
{
    static int printed;
    const char *trap = getenv("RECOMP_UNIMPL_TRAP");
    int stop = trap && *trap && *trap != '0';

    if (printed < 50 || stop) {
        printed++;
        fprintf(stderr,
                "[UNIMPL] untranslated instruction REACHED: `%s` at 0x%08X"
                " (a no-op; set RECOMP_UNIMPL_TRAP=1 to stop here)\n",
                text, va);
        fflush(stderr);
    }
    if (stop) abort();
}
