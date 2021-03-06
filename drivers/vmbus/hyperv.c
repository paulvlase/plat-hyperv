/*-
 * Copyright (c) 2009-2012,2016-2017 Microsoft Corp.
 * Copyright (c) 2012 NetApp Inc.
 * Copyright (c) 2012 Citrix Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * Implements low-level interactions with Hyper-V/Azure
 */
// #include <sys/cdefs.h>
// __FBSDID("$FreeBSD$");

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <x86/cpu.h>
#include <uk/config.h>
#include <uk/print.h>
#include <uk/plat/io.h>

#include <include/vmbus.h>
#include <include/hyperv.h>
// #include <dev/hyperv/include/hyperv_busdma.h>
#include <vmbus/hyperv_machdep.h>
#include <vmbus/hyperv_reg.h>
// #include <dev/hyperv/vmbus/hyperv_var.h>

#include <hyperv/bsd_layer.h>

#define HYPERV_UNIKRAFT_BUILD		0ULL
#define HYPERV_UNIKRAFT_VERSION		((uint64_t)0x0500)
#define HYPERV_UNIKRAFT_OSID		0ULL

#define MSR_HV_GUESTID_BUILD_UNIKRAFT	\
	(HYPERV_UNIKRAFT_BUILD & MSR_HV_GUESTID_BUILD_MASK)
#define MSR_HV_GUESTID_VERSION_UNIKRAFT	\
	((HYPERV_UNIKRAFT_VERSION << MSR_HV_GUESTID_VERSION_SHIFT) & \
	 MSR_HV_GUESTID_VERSION_MASK)
#define MSR_HV_GUESTID_OSID_UNIKRAFT	\
	((HYPERV_UNIKRAFT_OSID << MSR_HV_GUESTID_OSID_SHIFT) & \
	 MSR_HV_GUESTID_OSID_MASK)

#define MSR_HV_GUESTID_UNIKRAFT		\
	(MSR_HV_GUESTID_BUILD_UNIKRAFT |	\
	 MSR_HV_GUESTID_VERSION_UNIKRAFT | \
	 MSR_HV_GUESTID_OSID_UNIKRAFT |	\
	 MSR_HV_GUESTID_OSTYPE_UNIKRAFT)

struct hypercall_ctx {
	void			*hc_addr;
	__phys_addr		hc_paddr;
};

//static u_int		hyperv_get_timecount();
static bool			hyperv_identify(void);
static void			hypercall_create(void *arg __unused);
// static void			hypercall_memfree(void);

u_int				hyperv_ver_major;

u_int				hyperv_features;
u_int				hyperv_recommends;

static u_int			hyperv_pm_features;
static u_int			hyperv_features3;

hyperv_tc64_t			hyperv_tc64;

extern char hypercall_page[PAGE_SIZE];

static __inline void
do_cpuid(u_int ax, u_int *p)
{

	__asm __volatile("cpuid"
	    : "=a" (p[0]), "=b" (p[1]), "=c" (p[2]), "=d" (p[3])
	    :  "0" (ax));
}

static struct hypercall_ctx	hypercall_context;

void *hyperv_mem_alloc(struct uk_alloc *a, size_t size)
{
	return uk_calloc(a, 1, size);
}
u_int
hyperv_get_timecount()
{
	return rdmsrl(MSR_HV_TIME_REF_COUNT);
}

static uint64_t
hyperv_tc64_rdmsr(void)
{

	return (rdmsrl(MSR_HV_TIME_REF_COUNT));
}

uint64_t
hypercall_post_message(bus_addr_t msg_paddr)
{
	uk_pr_info("hypercall_post_message\n");
	return hypercall_md(hypercall_context.hc_addr,
	    HYPERCALL_POST_MESSAGE, msg_paddr, 0);
}

uint64_t
hypercall_signal_event(bus_addr_t monprm_paddr)
{
	return hypercall_md(hypercall_context.hc_addr,
	    HYPERCALL_SIGNAL_EVENT, monprm_paddr, 0);
}

// int
// hyperv_guid2str(const struct hyperv_guid *guid, char *buf, size_t sz)
// {
// 	const uint8_t *d = guid->hv_guid;

// 	return snprintf(buf, sz, "%02x%02x%02x%02x-"
// 	    "%02x%02x-%02x%02x-%02x%02x-"
// 	    "%02x%02x%02x%02x%02x%02x",
// 	    d[3], d[2], d[1], d[0],
// 	    d[5], d[4], d[7], d[6], d[8], d[9],
// 	    d[10], d[11], d[12], d[13], d[14], d[15]);
// }

static bool
hyperv_identify(void)
{
	u_int regs[4];
	unsigned int maxleaf;

	// if (vm_guest != VM_GUEST_HV)
	// 	return (false);

	do_cpuid(CPUID_LEAF_HV_MAXLEAF, regs);
	maxleaf = regs[0];
	if (maxleaf < CPUID_LEAF_HV_LIMITS)
		return (false);

	do_cpuid(CPUID_LEAF_HV_INTERFACE, regs);
	if (regs[0] != CPUID_HV_IFACE_HYPERV)
		return (false);

	do_cpuid(CPUID_LEAF_HV_FEATURES, regs);
	if ((regs[0] & CPUID_HV_MSR_HYPERCALL) == 0) {
		/*
		 * Hyper-V w/o Hypercall is impossible; someone
		 * is faking Hyper-V.
		 */
		return (false);
	}
	hyperv_features = regs[0];
	hyperv_pm_features = regs[2];
	hyperv_features3 = regs[3];

	do_cpuid(CPUID_LEAF_HV_IDENTITY, regs);
	hyperv_ver_major = regs[1] >> 16;
	printf("Hyper-V Version: %d.%d.%d [SP%d]\n",
	    hyperv_ver_major, regs[1] & 0xffff, regs[0], regs[2]);

	printf("  Features=0x%08x ", hyperv_features);
	printf("<");
	if (hyperv_features & (1<<0))
		printf("VPRUNTIME,");	/* MSR_HV_VP_RUNTIME */
	if (hyperv_features & (1<<1))
		printf("TMREFCNT,");	/* MSR_HV_TIME_REF_COUNT */
	if (hyperv_features & (1<<2))
	    printf("SYNIC,");		/* MSRs for SynIC */
	if (hyperv_features & (1<<3))
		printf("SYNTM,");		/* MSRs for SynTimer */
	if (hyperv_features & (1<<4))
	    printf("APIC,");			/* MSR_HV_{EOI,ICR,TPR} */
	if (hyperv_features & (1<<5))
	    printf("HYPERCALL,");	/* MSR_HV_{GUEST_OS_ID,HYPERCALL} */
	if (hyperv_features & (1<<6))
	    printf("VPINDEX,");		/* MSR_HV_VP_INDEX */
	if (hyperv_features & (1<<9))
	    printf("RESET,");		/* MSR_HV_RESET */
	if (hyperv_features & (1<<10))
	    printf("STATS,");		/* MSR_HV_STATS_ */
	if (hyperv_features & (1<<11))
	    printf("REFTSC,");		/* MSR_HV_REFERENCE_TSC */
	if (hyperv_features & (1<<12))
	    printf("IDLE,");			/* MSR_HV_GUEST_IDLE */
	if (hyperv_features & (1<<13))
	    printf("TMFREQ,");		/* MSR_HV_{TSC,APIC}_FREQUENCY */
	if (hyperv_features & (1<<14))
	    printf("DEBUG,");	/* MSR_HV_SYNTH_DEBUG_ */
	printf(">\n");
	
	printf("  PM Features=0x%08x ", (hyperv_pm_features & ~CPUPM_HV_CSTATE_MASK));
	printf("<");
	if ((hyperv_pm_features & ~CPUPM_HV_CSTATE_MASK) & (1<<4))
		printf("C3HPET,");	/* HPET is required for C3 state */
	printf("> ");
	printf("[C%u]\n", CPUPM_HV_CSTATE(hyperv_pm_features));
	
	printf("  Features3=0x%08x ", hyperv_features3);
	printf("<");
	if (hyperv_features3 & (1 << 0))
		printf("MWAIT,");		/* MWAIT */
	if (hyperv_features3 & (1 << 1))
		printf("DEBUG,");		/* guest debug support */
	if (hyperv_features3 & (1 << 2))
		printf("PERFMON,");	/* performance monitor */
	if (hyperv_features3 & (1 << 3))
		printf("PCPUDPE,");	/* physical CPU dynamic partition event */
	if (hyperv_features3 & (1 << 4))
		printf("XMMHC,");		/* hypercall input through XMM regs */
	if (hyperv_features3 & (1 << 5))
		printf("IDLE,");		/* guest idle support */
	if (hyperv_features3 & (1 << 6))
		printf("SLEEP,");		/* hypervisor sleep support */
	if (hyperv_features3 & (1 << 9))
		printf("NUMA,");		/* NUMA distance query support */
	if (hyperv_features3 & (1 << 10))
		printf("TMFREQ,");	/* timer frequency query (TSC, LAPIC) */
	if (hyperv_features3 & (1 << 11))
		printf("SYNCMC,");	/* inject synthetic machine checks */
	if (hyperv_features3 & (1 << 12))
		printf("CRASH,");		/* MSRs for guest crash */
	if (hyperv_features3 & (1 << 13))
		printf("DEBUGMSR,");	/* MSRs for guest debug */
	if (hyperv_features3 & (1 << 14))
		printf("NPIEP,");		/* NPIEP */
	if (hyperv_features3 & (1 << 15))
		printf("HVDIS,");	/* disabling hypervisor */
	printf(">\n");

	do_cpuid(CPUID_LEAF_HV_RECOMMENDS, regs);
	hyperv_recommends = regs[0];
	// if (bootverbose)
		printf("  Recommends: %08x %08x\n", regs[0], regs[1]);

	do_cpuid(CPUID_LEAF_HV_LIMITS, regs);
	// if (bootverbose) {
		printf("  Limits: Vcpu:%d Lcpu:%d Int:%d\n",
		    regs[0], regs[1], regs[2]);
	// }

	if (maxleaf >= CPUID_LEAF_HV_HWFEATURES) {
		do_cpuid(CPUID_LEAF_HV_HWFEATURES, regs);
		// if (bootverbose) {
			printf("  HW Features: %08x, AMD: %08x\n",
			    regs[0], regs[3]);
		// }
	}

	return (true);
}

void
hyperv_init(void *dummy __unused)
{
	if (!hyperv_identify()) {
		/* Not Hyper-V; reset guest id to the generic one. */
		// if (vm_guest == VM_GUEST_HV)
		// 	vm_guest = VM_GUEST_VM;
		return;
	}

	/* Set guest id */
	wrmsrl(MSR_HV_GUEST_OS_ID, MSR_HV_GUESTID_UNIKRAFT);

	hypercall_create(NULL);

	uk_pr_info("Hyper-v initialized!\n");
	if (hyperv_features & CPUID_HV_MSR_TIME_REFCNT) {
	// 	/*
	// 	 * Register Hyper-V timecounter.  This should be done as early
	// 	 * as possible to let DELAY() work, since the 8254 PIT is not
	// 	 * reliably emulated or even available.
	// 	 */
	// 	tc_init(&hyperv_timecounter);

		/*
		 * Install 64 bits timecounter method for other modules
		 * to use.
		 */
		hyperv_tc64 = hyperv_tc64_rdmsr;

		uk_pr_info("Hyper-v ref time counter: %u!\n", hyperv_get_timecount());
	}
}
// SYSINIT(hyperv_initialize, SI_SUB_HYPERVISOR, SI_ORDER_FIRST, hyperv_init,
//     NULL);

// static void
// hypercall_memfree(void)
// {
// 	kmem_free((vm_offset_t)hypercall_context.hc_addr, PAGE_SIZE);
// 	hypercall_context.hc_addr = NULL;
// }

static void
hypercall_create(void *arg __unused)
{
	uint64_t hc, hc_orig;

	// if (vm_guest != VM_GUEST_HV)
	// 	return;

	/*
	 * NOTE:
	 * - busdma(9), i.e. hyperv_dmamem APIs, can _not_ be used due to
	 *   the NX bit.
	 * - Assume kmem_malloc() returns properly aligned memory.
	 */
	//hypercall_context.hc_addr = (void *)vmbus_malloc(__PAGE_SIZE);
	// struct uk_alloc *a = vmbus_get_alloc();
	// hypercall_context.hc_addr = (void *)uk_memalign(a, __PAGE_SIZE, __PAGE_SIZE);
	hypercall_context.hc_addr = (void *)hypercall_page;
	hypercall_context.hc_paddr = ukplat_virt_to_phys(hypercall_context.hc_addr);
	printf("hyperv: Hypercall page allocated addr: 0x%p paddr: 0x%lx\n", hypercall_context.hc_addr, hypercall_context.hc_paddr);
	/* Get the 'reserved' bits, which requires preservation. */
	hc_orig = rdmsrl(MSR_HV_HYPERCALL);

	/*
	 * Setup the Hypercall page.
	 *
	 * NOTE: 'reserved' bits MUST be preserved.
	 */
	hc = ((hypercall_context.hc_paddr >> __PAGE_SHIFT) <<
	    MSR_HV_HYPERCALL_PGSHIFT) |
	    (hc_orig & MSR_HV_HYPERCALL_RSVD_MASK) |
	    MSR_HV_HYPERCALL_ENABLE;
	wrmsrl(MSR_HV_HYPERCALL, hc);

	/*
	 * Confirm that Hypercall page did get setup.
	 */
	hc = rdmsrl(MSR_HV_HYPERCALL);
	if ((hc & MSR_HV_HYPERCALL_ENABLE) == 0) {
		printf("hyperv: Hypercall create failed hc: 0x%lx\n", hc);
		// hypercall_memfree();
		/* Can't perform any Hyper-V specific actions */
		// vm_guest = VM_GUEST_VM;
		return;
	}
// 	if (bootverbose)
		printf("hyperv: Hypercall initialized!\n");
}
// SYSINIT(hypercall_ctor, SI_SUB_DRIVERS, SI_ORDER_FIRST, hypercall_create, NULL);

// static void
// hypercall_destroy(void *arg __unused)
// {
// 	uint64_t hc;

// 	if (hypercall_context.hc_addr == NULL)
// 		return;

// 	/* Disable Hypercall */
// 	hc = rdmsrl(MSR_HV_HYPERCALL);
// 	wrmsr(MSR_HV_HYPERCALL, (hc & MSR_HV_HYPERCALL_RSVD_MASK));
// 	hypercall_memfree();

// 	if (bootverbose)
// 		printf("hyperv: Hypercall destroyed\n");
// }
// SYSUNINIT(hypercall_dtor, SI_SUB_DRIVERS, SI_ORDER_FIRST, hypercall_destroy,
//     NULL);
