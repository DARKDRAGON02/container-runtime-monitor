# Container Runtime with Kernel-Level Memory Monitoring

A container runtime written from scratch in C — no Docker, no libcontainer, no cgroups. Isolation comes from `clone()` with Linux namespaces; memory limits come from a **custom kernel module** that reads each container's RSS and kills the process when it crosses a hard limit.

Built as a two-person systems project around one concrete question: what actually happens between `docker run` and a running process?

## Architecture

Two components that meet in the kernel:

| Component | File | Responsibility |
|---|---|---|
| **Engine** (user space) | `engine.c` | Supervisor CLI. Spawns containers with isolated namespaces, logs their output, tracks state. |
| **Monitor** (kernel space) | `monitor.c` | Loadable module. Takes a PID, samples its RSS, warns at the soft limit, kills at the hard limit. |

```
engine.c  ──clone(CLONE_NEWPID|NEWUTS|NEWNS)──▶  container process
   │                                                     │
   │ register_pid(pid)                                   │ RSS sampled
   ▼                                                     ▼
monitor.ko  ──────────── SOFT 40 MB → warn ──────────────┤
            ──────────── HARD 64 MB → SIGKILL ───────────┘
```

## Features

- **Process isolation** via `CLONE_NEWPID | CLONE_NEWUTS | CLONE_NEWNS`
- **Filesystem isolation** with `chroot()` into a per-container rootfs, plus a private `/proc` mount
- **Supervisor CLI** — `start`, `stop`, `ps`, `exit`
- **Captured container output** — a logger thread drains the child's stdout/stderr into `<id>.log`
- **Kernel-mediated memory enforcement** — RSS read from `get_mm_rss()`, converted to KB and compared against soft and hard ceilings
- **Two-stage response** — one `printk` warning before the kill, so you can see the approach in `dmesg`

## Build and run

Requires Linux, kernel headers for your running kernel, and root.

```bash
# 1. Build and load the kernel module
make
sudo insmod monitor.ko

# 2. Build the userspace engine (note: the Makefile targets the module only)
gcc engine.c -o engine -pthread

# 3. Start the supervisor
sudo ./engine
```

Then drive it from the supervisor prompt:

```
>> start c1 /path/to/rootfs
Started container c1 with PID 4821

>> ps
ID      PID     STATE
c1      4821    RUNNING

>> stop c1
Stopped container c1
```

Watch the enforcement happen in another terminal:

```bash
dmesg -w
# PID 4821 SOFT limit: 41208 KB
# PID 4821 HARD limit → KILL
```

### The rootfs

`start` takes a rootfs directory that must contain a working `/bin/sh` and `python3` — the demo workload is a `python3` one-liner that allocates ~200 MB, specifically so it trips both limits. A minimal `busybox` rootfs works.

## How enforcement works

1. `engine.c` calls `clone()` with fresh PID, UTS and mount namespaces.
2. The child `chroot()`s into its rootfs, mounts a private `/proc`, and `exec`s its workload.
3. The engine hands the new PID to the module via `register_pid()`.
4. `monitor.c` samples that task's RSS on a timer.
5. Above **SOFT (40,000 KB)** → warn once. Above **HARD (64,000 KB)** → `SIGKILL` and log.

Both thresholds are `#define`s at the top of `monitor.c`.

## Deliberate limitations

This is a learning implementation, and these are the parts I know it gets wrong — worth stating plainly rather than leaving for someone to discover:

- **Limits are compile-time constants.** Moving them per-container requires exposing a write path (a `/proc` entry or ioctl) and storing per-PID state — the module currently holds a single global.
- **RSS is sampled, not enforced by the memory controller.** A process can overshoot between samples, and RSS counts resident pages only — no accounting of page cache or swap.
- **`chroot` is not a security boundary.** With root inside, escape is trivial. A real runtime needs pivot_root plus user namespaces and a dropped capability set.
- **No cgroups, no seccomp, no capability dropping.** Deliberate: the point was to build the mechanism, not to reimplement runc.
- **The workload is hardcoded** in `child_func()` rather than passed as a command.

The natural next step is replacing `chroot` with `pivot_root` + user namespaces, and moving limits into cgroup v2 so the kernel enforces them without a polling module.

## Team

A two-person project: the engine, logging and supervisor CLI in one half; the kernel module and memory monitoring in the other. Maintained by [@DARKDRAGON02](https://github.com/DARKDRAGON02).
