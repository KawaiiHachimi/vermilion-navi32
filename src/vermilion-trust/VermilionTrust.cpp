// SPDX-License-Identifier: MIT
//
// VermilionTrust: keep AMFI (library validation) on for the whole system, and let through only Vermilion's own
// ad-hoc signed code, identified by cdhash. It replaces the global boot-arg amfi=0x80 (AMFI "allow everything").
//
// How: AMFI is a MAC policy. Its operation vector (struct mac_policy_ops) holds the two hooks that enforce library
// validation: mpo_file_check_library_validation (dyld's F_CHECK_LV) and mpo_file_check_mmap (an executable mapping;
// dyld's map_with_linking_np goes through it too), plus mpo_file_check_mmap_downgrade. This kext swaps those
// pointers for wrappers that call AMFI first. Only when AMFI refuses, and the file's code directory hash is in the
// list compiled into this kext (phxtrust_list.inc), the wrapper allows it. Every other decision is AMFI's, unchanged. The list can't change on a running system: it lives in this binary on the ESP.
//
// Layouts are from XNU xnu-12377.121.6 (macOS 26): security/mac_internal.h (mac_policy_list), security/mac_policy.h
// (mac_policy_conf, mac_policy_ops: 335 pointers, no conditional members), bsd/sys/file_internal.h (fileglob).
// The running kernel on the XTX machine is xnu-12377.161.15 (26.7.1 25G241); the offsets are checked at runtime
// (non-null, close to the vector) before anything is written, and nothing is patched if a check fails.
//
// Boot-args: -phxtrustoff (don't load), -phxtrustlog (also count every check of a listed file and every executable
// mapping of unlisted ad-hoc code, for a boot that still has amfi=0x80), -phxtrustdbg (Lilu debug output).
// Counters: sysctl debug.phxtrust_stat; per file: sysctl debug.phxtrust_files.

#include <Headers/plugin_start.hpp>
#include <Headers/kern_api.hpp>
#include <Headers/kern_patcher.hpp>
#include <Headers/kern_mach.hpp>
#include <Headers/kern_util.hpp>

#include <IOKit/IOLib.h>
#include <libkern/libkern.h>
#include <sys/mman.h>
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <sys/vnode.h>
#include <kern/clock.h>
#include <kern/thread_call.h>

#include "phxtrust_list.inc" // kPhxTrustList[][20], kPhxTrustNames[], kPhxTrustCount (gen_list.sh)

// ------------------------------------------------------------------------------------------------ kernel layouts

namespace {

constexpr size_t kCdHashLen = 20;          // CS_CDHASH_LEN
constexpr size_t kOpsDowngrade = 280;      // offsetof(struct mac_policy_ops, mpo_file_check_mmap_downgrade)
constexpr size_t kOpsMmap = 288;           // offsetof(struct mac_policy_ops, mpo_file_check_mmap)
constexpr size_t kOpsLibVal = 512;         // offsetof(struct mac_policy_ops, mpo_file_check_library_validation)
constexpr size_t kOpsCheckSig = 2432;      // offsetof(struct mac_policy_ops, mpo_vnode_check_signature) (logged only)
constexpr size_t kFgOps = 40;              // offsetof(struct fileglob, fg_ops)
constexpr size_t kFgData = 56;             // offsetof(struct fileglob, fg_data)
constexpr int kDtypeVnode = 1;             // DTYPE_VNODE
constexpr mach_vm_address_t kNear = 256ULL << 20; // hooks and their vector both sit in AMFI, in the boot KC

struct MacPolicyConf {                     // struct mac_policy_conf
	const char *mpc_name;
	const char *mpc_fullname;
	const char *const *mpc_labelnames;
	unsigned int mpc_labelname_count;
	void *mpc_ops;                     // const struct mac_policy_ops *
	int mpc_loadtime_flags;
	int *mpc_field_off;
	int mpc_runtime_flags;
	MacPolicyConf *mpc_list;
	void *mpc_data;
};
static_assert(__builtin_offsetof(MacPolicyConf, mpc_ops) == 32, "mac_policy_conf layout");

struct MacPolicyList {                     // struct mac_policy_list
	u_int numloaded, max, maxindex, staticmax, chunks, freehint;
	MacPolicyConf **entries;           // struct mac_policy_list_element { struct mac_policy_conf *mpc; } *
};

using LibValFn = int (*)(proc_t, void *fg, off_t slice_offset, int64_t error_message, size_t error_message_size);
using MmapFn = int (*)(kauth_cred_t, void *fg, void *label, int prot, int flags, uint64_t file_pos, int *maxprot);
using DowngradeFn = void (*)(kauth_cred_t, void *fg, void *label, int *prot);
using BlobGetFn = void *(*)(vnode_t, cpu_type_t, cpu_subtype_t, off_t);
using BlobCdHashFn = const uint8_t *(*)(void *);
using BlobFlagsFn = unsigned int (*)(void *);

LibValFn orgLibVal;
MmapFn orgMmap;
DowngradeFn orgDowngrade;
BlobGetFn blobGet;
BlobCdHashFn blobCdHash;
BlobFlagsFn blobFlags;

constexpr unsigned int kCsAdhoc = 0x2;     // CS_ADHOC, a code directory flag: the same with or without amfi=0x80

bool logMode;
const char *installState = "not installed";

template <typename T> inline void inc(T &v) { __atomic_fetch_add(&v, 1, __ATOMIC_RELAXED); }

// Counters (relaxed atomics; read by debug.phxtrust_stat).
struct Stat {
	uint64_t libvalCalls, libvalDenied, mmapExecCalls, mmapExecDenied, overrides, deniedUnlisted;
} st;

// Per listed file (debug.phxtrust_files): checks seen (log mode), AMFI refusals overridden, first process.
struct ListedStat {
	uint64_t libval, mmapExec, overrides;
	uint32_t claimed;
	char firstProc[32];
} listedStat[kPhxTrustCount];

// Log mode: ad-hoc code that is not on the list, mapped executable (what a real AMFI would refuse in a platform
// process). Enforce mode: every file AMFI refused (library validation or an executable mapping) that is not on the
// list. One slot per cdhash, first come first served.
constexpr size_t kSeen = 32;
struct Seen {
	uint32_t state;                    // 0 free, 1 being filled, 2 ready
	uint8_t cd[kCdHashLen];
	uint64_t count;
	char proc[32];
	char path[160];
} seen[kSeen];
uint64_t seenDropped;

// Kernel log lines (overrides only) are capped so a busy boot can't flood the log.
constexpr uint32_t kMaxLog = 64;
uint32_t logged;

// ------------------------------------------------------------------------------------------------ helpers

vnode_t fgVnode(void *fg) {
	if (!fg) return nullptr;
	auto ops = *reinterpret_cast<const int *const *>(static_cast<uint8_t *>(fg) + kFgOps);
	if (!ops || *ops != kDtypeVnode) return nullptr;
	return *reinterpret_cast<vnode_t *>(static_cast<uint8_t *>(fg) + kFgData);
}

int listIndex(const uint8_t *cd) {
	if (!cd) return -1;
	for (size_t i = 0; i < kPhxTrustCount; i++)
		if (memcmp(cd, kPhxTrustList[i], kCdHashLen) == 0) return static_cast<int>(i);
	return -1;
}

void hex(const uint8_t *cd, char out[2 * kCdHashLen + 1]) {
	static const char d[] = "0123456789abcdef";
	for (size_t i = 0; i < kCdHashLen; i++) {
		out[2 * i] = cd ? d[cd[i] >> 4] : '?';
		out[2 * i + 1] = cd ? d[cd[i] & 15] : '?';
	}
	out[2 * kCdHashLen] = 0;
}

struct FileInfo {
	const uint8_t *cd = nullptr;
	bool adhoc = false;
	int idx = -1;
};

FileInfo fileInfo(vnode_t vp, off_t offset) {
	FileInfo fi;
	if (!vp || !blobGet || !blobCdHash) return fi;
	void *blob = blobGet(vp, -1, -1, offset);
	if (!blob) return fi;
	fi.cd = blobCdHash(blob);
	fi.adhoc = blobFlags && (blobFlags(blob) & kCsAdhoc);
	fi.idx = listIndex(fi.cd);
	return fi;
}

void procName(proc_t p, char *out, int size) {
	strlcpy(out, "?", static_cast<size_t>(size));
	if (p) proc_name(proc_pid(p), out, size);
}

void noteListed(int idx, proc_t p) {
	ListedStat &l = listedStat[idx];
	uint32_t z = 0;
	if (__atomic_load_n(&l.claimed, __ATOMIC_RELAXED) == 0 &&
	    __atomic_compare_exchange_n(&l.claimed, &z, 1, false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED))
		procName(p, l.firstProc, sizeof(l.firstProc));
}

void noteUnlisted(const FileInfo &fi, proc_t p, vnode_t vp) {
	for (size_t i = 0; i < kSeen; i++) {
		Seen &s = seen[i];
		uint32_t state = __atomic_load_n(&s.state, __ATOMIC_ACQUIRE);
		if (state == 2 && memcmp(s.cd, fi.cd, kCdHashLen) == 0) { inc(s.count); return; }
		if (state == 0) {
			uint32_t z = 0;
			if (!__atomic_compare_exchange_n(&s.state, &z, 1, false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) continue;
			memcpy(s.cd, fi.cd, kCdHashLen);
			s.count = 1;
			procName(p, s.proc, sizeof(s.proc));
			int len = sizeof(s.path);
			if (!vp || vn_getpath(vp, s.path, &len) != 0) strlcpy(s.path, "?", sizeof(s.path));
			__atomic_store_n(&s.state, 2, __ATOMIC_RELEASE);
			return;
		}
	}
	inc(seenDropped);
}

// Log mode: account for a check AMFI allowed (amfi=0x80 allows everything).
void observe(const FileInfo &fi, proc_t p, vnode_t vp, bool libval) {
	if (fi.idx >= 0) {
		inc(libval ? listedStat[fi.idx].libval : listedStat[fi.idx].mmapExec);
		noteListed(fi.idx, p);
	} else if (fi.cd && fi.adhoc && !libval) {
		noteUnlisted(fi, p, vp);
	}
}

void override(const char *hook, proc_t p, const FileInfo &fi, int amfi) {
	inc(st.overrides);
	inc(listedStat[fi.idx].overrides);
	noteListed(fi.idx, p);
	if (__atomic_fetch_add(&logged, 1, __ATOMIC_RELAXED) >= kMaxLog) return;
	char name[32];
	procName(p, name, sizeof(name));
	IOLog("VermilionTrust: allowed %s for %s(%d) (AMFI said %d)\n", kPhxTrustNames[fi.idx], name, p ? proc_pid(p) : -1,
	      amfi);
}

// ------------------------------------------------------------------------------------------------ the hooks

int wrapLibVal(proc_t p, void *fg, off_t slice_offset, int64_t msg, size_t msgSize) {
	inc(st.libvalCalls);
	int r = orgLibVal(p, fg, slice_offset, msg, msgSize);
	if (r == 0 && !logMode) return 0;
	vnode_t vp = fgVnode(fg);
	FileInfo fi = fileInfo(vp, slice_offset);
	if (r == 0) {
		observe(fi, p, vp, true);
		return 0;
	}
	inc(st.libvalDenied);
	if (fi.idx >= 0) {
		override("library validation", p, fi, r);
		return 0;
	}
	inc(st.deniedUnlisted);
	if (!logMode && fi.cd) noteUnlisted(fi, p, vp);
	return r;
}

// Only mappings that ask for PROT_EXEC are looked at. AMFI also takes PROT_EXEC out of the maximum protection of
// ordinary data mappings; that is normal and left alone (b1.0.0 counted and undid it: 19.8 k times on its boot).
int wrapMmap(kauth_cred_t cred, void *fg, void *label, int prot, int flags, uint64_t pos, int *maxprot) {
	if (!(prot & PROT_EXEC)) return orgMmap(cred, fg, label, prot, flags, pos, maxprot);
	inc(st.mmapExecCalls);
	const int before = maxprot ? *maxprot : 0;
	int r = orgMmap(cred, fg, label, prot, flags, pos, maxprot);
	const bool lostExec = maxprot && (before & PROT_EXEC) && !(*maxprot & PROT_EXEC);
	if (r == 0 && !lostExec && !logMode) return 0;
	vnode_t vp = fgVnode(fg);
	FileInfo fi = fileInfo(vp, static_cast<off_t>(pos));
	proc_t p = current_proc();
	if (r == 0 && !lostExec) {
		observe(fi, p, vp, false);
		return 0;
	}
	inc(st.mmapExecDenied);
	if (fi.idx >= 0) {
		if (maxprot) *maxprot = before;
		override("executable mapping", p, fi, r ? r : -1);
		return 0;
	}
	inc(st.deniedUnlisted);
	if (!logMode && fi.cd) noteUnlisted(fi, p, vp);
	return r;
}

// Not called by xnu-12377.121.6 itself, but present in the kernel (_mac_file_check_mmap_downgrade): if AMFI ever
// strips PROT_EXEC here instead of refusing in file_check_mmap, a listed file keeps it.
void wrapDowngrade(kauth_cred_t cred, void *fg, void *label, int *prot) {
	const int before = prot ? *prot : 0;
	orgDowngrade(cred, fg, label, prot);
	if (!prot || !(before & PROT_EXEC) || (*prot & PROT_EXEC)) return;
	inc(st.mmapExecDenied);
	FileInfo fi = fileInfo(fgVnode(fg), 0);
	if (fi.idx >= 0) {
		*prot = before;
		override("downgrade", current_proc(), fi, -1);
		return;
	}
	inc(st.deniedUnlisted);
}

// ------------------------------------------------------------------------------------------------ install

bool near(mach_vm_address_t a, mach_vm_address_t b) { return (a > b ? a - b : b - a) < kNear; }

// AMFI registers its MAC policy from its own start routine; Lilu's patcher may be ready before that. Then install()
// is retried every 100 ms for up to 60 s (long before WindowServer maps the plugin).
constexpr uint32_t kRetryMs = 100, kRetryMax = 600;
KernelPatcher *patcher;
thread_call_t retryCall;
uint32_t retries;

bool install(KernelPatcher &kp, bool last);

void retry(thread_call_param_t, thread_call_param_t) {
	retries++;
	if (install(*patcher, retries >= kRetryMax) || retries >= kRetryMax) return;
	uint64_t deadline;
	clock_interval_to_deadline(kRetryMs, kMillisecondScale, &deadline);
	thread_call_enter_delayed(retryCall, deadline);
}

bool install(KernelPatcher &kp, bool last) {
	auto list = reinterpret_cast<MacPolicyList *>(kp.solveSymbol(KernelPatcher::KernelID, "_mac_policy_list"));
	blobGet = reinterpret_cast<BlobGetFn>(kp.solveSymbol(KernelPatcher::KernelID, "_ubc_cs_blob_get"));
	blobCdHash = reinterpret_cast<BlobCdHashFn>(kp.solveSymbol(KernelPatcher::KernelID, "_csblob_get_cdhash"));
	blobFlags = reinterpret_cast<BlobFlagsFn>(kp.solveSymbol(KernelPatcher::KernelID, "_csblob_get_flags"));
	kp.clearError();
	if (!list || !blobGet || !blobCdHash) {
		installState = "symbols missing";
		IOLog("VermilionTrust: not installed: mac_policy_list %d ubc_cs_blob_get %d csblob_get_cdhash %d\n",
		      list != nullptr, blobGet != nullptr, blobCdHash != nullptr);
		return true; // final: retrying can't help
	}

	MacPolicyConf *amfi = nullptr;
	for (u_int i = 0; list->entries && i <= list->maxindex && i < list->max; i++) {
		MacPolicyConf *c = list->entries[i];
		if (!c || !c->mpc_name) continue;
		if (logMode && (last || retries == 0)) IOLog("VermilionTrust: MAC policy %u: %s\n", i, c->mpc_name);
		if (strcmp(c->mpc_name, "AMFI") == 0) amfi = c;
	}
	if (!amfi || !amfi->mpc_ops) {
		installState = last ? "AMFI policy not found" : "waiting for AMFI";
		if (last || retries == 0) IOLog("VermilionTrust: no AMFI policy in mac_policy_list yet (try %u)%s\n", retries,
		                                last ? ", giving up" : ", retrying");
		return false;
	}
	if (retries) IOLog("VermilionTrust: AMFI policy found after %u retries\n", retries);

	auto ops = static_cast<uint8_t *>(amfi->mpc_ops);
	auto slot = [&](size_t off) { return reinterpret_cast<mach_vm_address_t *>(ops + off); };
	const mach_vm_address_t base = reinterpret_cast<mach_vm_address_t>(ops);
	const mach_vm_address_t lv = *slot(kOpsLibVal), mm = *slot(kOpsMmap), dg = *slot(kOpsDowngrade), sig = *slot(kOpsCheckSig);
	IOLog("VermilionTrust: AMFI ops %p: file_check_library_validation %llx file_check_mmap %llx file_check_mmap_downgrade %llx "
	      "vnode_check_signature %llx; list %u entries; mode %s\n", ops, lv, mm, dg, sig, static_cast<unsigned>(kPhxTrustCount),
	      logMode ? "log" : "enforce");
	if (!lv || !mm || !near(lv, base) || !near(mm, base) || lv == mm || (dg && (!near(dg, base) || dg == lv || dg == mm))) {
		installState = "AMFI ops layout unexpected";
		IOLog("VermilionTrust: not installed: AMFI ops layout unexpected\n");
		return true;
	}

	orgLibVal = reinterpret_cast<LibValFn>(lv);
	orgMmap = reinterpret_cast<MmapFn>(mm);
	orgDowngrade = reinterpret_cast<DowngradeFn>(dg);
	if (MachInfo::setKernelWriting(true, KernelPatcher::kernelWriteLock) != KERN_SUCCESS) {
		installState = "kernel writing refused";
		IOLog("VermilionTrust: not installed: kernel writing refused\n");
		return true;
	}
	__atomic_store_n(slot(kOpsLibVal), reinterpret_cast<mach_vm_address_t>(&wrapLibVal), __ATOMIC_SEQ_CST);
	__atomic_store_n(slot(kOpsMmap), reinterpret_cast<mach_vm_address_t>(&wrapMmap), __ATOMIC_SEQ_CST);
	if (dg) __atomic_store_n(slot(kOpsDowngrade), reinterpret_cast<mach_vm_address_t>(&wrapDowngrade), __ATOMIC_SEQ_CST);
	MachInfo::setKernelWriting(false, KernelPatcher::kernelWriteLock);
	installState = "installed";
	IOLog("VermilionTrust: installed\n");
	return true;
}

// ------------------------------------------------------------------------------------------------ sysctl

int statSysctl(struct sysctl_oid *, void *, int, struct sysctl_req *req) {
	if (req->newptr) return EPERM;
	char b[512];
	int len = snprintf(b, sizeof(b),
	                   "state=%s mode=%s list=%u libval_calls=%llu libval_denied=%llu mmap_exec=%llu mmap_exec_denied=%llu "
	                   "overrides=%llu denied_unlisted=%llu\n",
	                   installState, logMode ? "log" : "enforce", static_cast<unsigned>(kPhxTrustCount), st.libvalCalls,
	                   st.libvalDenied, st.mmapExecCalls, st.mmapExecDenied, st.overrides, st.deniedUnlisted);
	if (len < 0) len = 0;
	if (len > static_cast<int>(sizeof(b)) - 1) len = sizeof(b) - 1;
	return SYSCTL_OUT(req, b, static_cast<size_t>(len) + 1);
}
SYSCTL_PROC(_debug, OID_AUTO, phxtrust_stat, CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_LOCKED, nullptr, 0, statSysctl, "A",
            "VermilionTrust: install state, mode, AMFI library-validation calls, refusals, overrides");

// One line per listed file, then (log mode) the unlisted ad-hoc code mapped executable.
int filesSysctl(struct sysctl_oid *, void *, int, struct sysctl_req *req) {
	if (req->newptr) return EPERM;
	constexpr int kN = 12288;
	char *b = static_cast<char *>(IOMalloc(kN));
	if (!b) return ENOMEM;
	int len = 0;
	auto add = [&](int n) { if (n > 0) len = (len + n < kN) ? len + n : kN - 1; };
	char cd[2 * kCdHashLen + 1];
	for (size_t i = 0; i < kPhxTrustCount; i++) {
		const ListedStat &l = listedStat[i];
		hex(kPhxTrustList[i], cd);
		add(snprintf(b + len, static_cast<size_t>(kN - len), "listed %s %s libval=%llu mmap_exec=%llu overrides=%llu first=%s\n", cd,
		             kPhxTrustNames[i], l.libval, l.mmapExec, l.overrides, l.claimed ? l.firstProc : "-"));
	}
	for (size_t i = 0; i < kSeen; i++) {
		const Seen &s = seen[i];
		if (__atomic_load_n(&s.state, __ATOMIC_ACQUIRE) != 2) continue;
		hex(s.cd, cd);
		add(snprintf(b + len, static_cast<size_t>(kN - len), "%s %s count=%llu first=%s path=%s\n",
		             logMode ? "unlisted-adhoc" : "refused", cd, s.count, s.proc, s.path));
	}
	add(snprintf(b + len, static_cast<size_t>(kN - len), "%s dropped=%llu\n", logMode ? "unlisted-adhoc" : "refused",
	             seenDropped));
	b[len] = 0;
	const int e = SYSCTL_OUT(req, b, static_cast<size_t>(len) + 1);
	IOFree(b, kN);
	return e;
}
SYSCTL_PROC(_debug, OID_AUTO, phxtrust_files, CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_LOCKED, nullptr, 0, filesSysctl, "A",
            "VermilionTrust: per listed file, checks and overrides; log mode: unlisted ad-hoc code mapped executable");

void pluginStart() {
	logMode = checkKernelArgument("-phxtrustlog");
	sysctl_register_oid(&sysctl__debug_phxtrust_stat);
	sysctl_register_oid(&sysctl__debug_phxtrust_files);
	lilu.onPatcherLoadForce([](void *, KernelPatcher &kp) {
		patcher = &kp;
		if (install(kp, false)) return;
		retryCall = thread_call_allocate(retry, nullptr);
		if (!retryCall) {
			installState = "no thread call for the retry";
			IOLog("VermilionTrust: not installed: thread_call_allocate failed\n");
			return;
		}
		uint64_t deadline;
		clock_interval_to_deadline(kRetryMs, kMillisecondScale, &deadline);
		thread_call_enter_delayed(retryCall, deadline);
	});
}

const char *bootargOff[] {"-phxtrustoff"};
const char *bootargDebug[] {"-phxtrustdbg"};
const char *bootargBeta[] {"-phxtrustbeta"};

} // namespace

PluginConfiguration ADDPR(config) {
	xStringify(PRODUCT_NAME),
	parseModuleVersion(xStringify(MODULE_VERSION)),
	LiluAPI::AllowNormal | LiluAPI::AllowSafeMode,
	bootargOff, arrsize(bootargOff),
	bootargDebug, arrsize(bootargDebug),
	bootargBeta, arrsize(bootargBeta),
	KernelVersion::Tahoe,
	KernelVersion::Tahoe,
	pluginStart
};
