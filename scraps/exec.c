// Minimal no-op wrapper binary to run specified binary with arguments
// Build with: gcc -O2 exec.c -o exec
// Usage example: ./exec ls /

#include <err.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
	char **cmdargv;
	cmdargv = &argv[1];
	execvp(cmdargv[0], cmdargv);
	err(1, "execvp(%s, ...)", cmdargv[0]);
	return 1;
}
