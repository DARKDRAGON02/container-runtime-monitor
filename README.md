# Container Runtime with Kernel Memory Monitoring

## Overview
A lightweight container runtime using Linux namespaces and a kernel module for memory monitoring.

## Features
- Container creation using clone()
- Namespace isolation (PID, UTS, mount)
- Supervisor CLI (start, stop, ps)
- Logging system
- Kernel memory monitoring (RSS)
- Soft and hard memory limits
- Automatic process termination

## Technologies
- C (User space + Kernel)
- Linux System Programming

## How it Works
1. Engine creates container
2. PID sent to kernel module
3. Kernel monitors memory
4. If limit exceeded → process killed

## Team
- Student 1: Container runtime, logging, supervisor
- Student 2: Kernel module, monitoring
