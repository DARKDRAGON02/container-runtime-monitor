#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/mount.h>
#include <signal.h>
#include <pthread.h>
#include <fcntl.h>

#define STACK_SIZE (1024 * 1024)
#define MAX_CONTAINERS 10

static char child_stack[STACK_SIZE];

typedef struct {
    char id[20];
    char rootfs[50];
    pid_t pid;
    int pipe_fd[2];
    int running;
} Container;

Container containers[MAX_CONTAINERS];
int container_count = 0;

// ---------------- CHILD ----------------
int child_func(void *arg) {
    Container *c = (Container *)arg;

    close(c->pipe_fd[0]);

    dup2(c->pipe_fd[1], 1);
    dup2(c->pipe_fd[1], 2);
    close(c->pipe_fd[1]);

    if (chroot(c->rootfs) != 0) {
        perror("chroot");
        return 1;
    }

    chdir("/");
    mount("proc", "/proc", "proc", 0, NULL);

    // MEMORY HEAVY WORKLOAD
    execl("/bin/sh", "/bin/sh", "-c",
          "python3 -c 'a=[\"A\"*1024*1024 for _ in range(200)]; import time; time.sleep(100)'",
          NULL);

    perror("execl");
    return 1;
}

// ---------------- LOGGER ----------------
void *logger_thread(void *arg) {
    Container *c = (Container *)arg;

    char filename[50];
    sprintf(filename, "%s.log", c->id);

    FILE *log = fopen(filename, "w");
    char buffer[256];

    while (1) {
        int n = read(c->pipe_fd[0], buffer, sizeof(buffer) - 1);
        if (n <= 0) break;

        buffer[n] = '\0';
        fprintf(log, "%s", buffer);
        fflush(log);
    }

    fclose(log);
    return NULL;
}

// ---------------- REGISTER PID ----------------
void register_pid(pid_t pid) {
    printf("Trying to send PID %d...\n", pid);

    int fd = open("/dev/container_monitor", O_WRONLY);

    if (fd < 0) {
        perror("❌ open failed");
        return;
    }

    char buf[32];
    sprintf(buf, "%d", pid);

    int ret = write(fd, buf, strlen(buf));

    if (ret < 0) {
        perror("❌ write failed");
    } else {
        printf("✅ PID %d sent successfully\n", pid);
    }

    close(fd);
}

// ---------------- START ----------------
void start_container(char *id, char *rootfs) {
    Container *c = &containers[container_count];

    strcpy(c->id, id);
    strcpy(c->rootfs, rootfs);
    c->running = 1;

    pipe(c->pipe_fd);

    int flags = CLONE_NEWPID | CLONE_NEWUTS | CLONE_NEWNS | SIGCHLD;

    pid_t pid = clone(child_func, child_stack + STACK_SIZE, flags, c);

    if (pid < 0) {
        perror("clone");
        return;
    }

    c->pid = pid;
    container_count++;

    close(c->pipe_fd[1]);

    pthread_t tid;
    pthread_create(&tid, NULL, logger_thread, c);
    pthread_detach(tid);

    // 🔥 SEND PID TO KERNEL
    register_pid(pid);

    printf("Started container %s with PID %d\n", id, pid);
}

// ---------------- STOP ----------------
void stop_container(char *id) {
    for (int i = 0; i < container_count; i++) {
        if (strcmp(containers[i].id, id) == 0 && containers[i].running) {
            kill(containers[i].pid, SIGTERM);
            containers[i].running = 0;
            printf("Stopped container %s\n", id);
            return;
        }
    }
}

// ---------------- PS ----------------
void list_containers() {
    printf("ID\tPID\tSTATE\n");
    for (int i = 0; i < container_count; i++) {
        printf("%s\t%d\t%s\n",
               containers[i].id,
               containers[i].pid,
               containers[i].running ? "RUNNING" : "STOPPED");
    }
}

// ---------------- MAIN ----------------
int main() {
    char command[100];

    printf("Supervisor started...\n");

    while (1) {
        printf(">> ");
        fflush(stdout);

        fgets(command, sizeof(command), stdin);

        if (strncmp(command, "start", 5) == 0) {
            char id[20], rootfs[50];
            sscanf(command, "start %s %s", id, rootfs);
            start_container(id, rootfs);
        }
        else if (strncmp(command, "stop", 4) == 0) {
            char id[20];
            sscanf(command, "stop %s", id);
            stop_container(id);
        }
        else if (strncmp(command, "ps", 2) == 0) {
            list_containers();
        }
        else if (strncmp(command, "exit", 4) == 0) {
            break;
        }
    }

    return 0;
}
