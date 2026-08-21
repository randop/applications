#include <stdio.h>
#include <unistd.h>
#include <pwd.h>

#define TARGET_USER "randolph"
#define CGROUP_PROCS "/sys/fs/cgroup/user/randolph/cgroup.procs"

int main(void) {
    uid_t caller = getuid();
    struct passwd *pw = getpwnam(TARGET_USER);
    if (!pw || caller != pw->pw_uid) {
        fprintf(stderr, "cgjoin: not authorized\n");
        return 1;
    }

    pid_t ppid = getppid();
    FILE *f = fopen(CGROUP_PROCS, "w");
    if (!f) { perror("fopen"); return 1; }
    fprintf(f, "%d\n", ppid);
    if (fclose(f) != 0) { perror("write"); return 1; }
    return 0;
}
