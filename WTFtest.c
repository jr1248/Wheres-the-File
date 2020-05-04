#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/wait.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <openssl/sha.h>


int main(int argc, char** agrv) {
  pid_t child = fork();
  if (child < 0) {
    perror("fork() error\n");
    exit(-1);
  }
  if (child != 0) {
    wait(NULL);
  }
  else {
    execlp("./WTFserver", "9123");
    sleep(5);
    execlp("./WTF", "configure", "cpp.cs.rutgers.edu", "9123");
    sleep(5);
    execlp("./WTF", "create", "testFile");
    sleep(5);
    execlp("./WTF", "add", "testFile","cs.txt");
    sleep(5);
    execlp("./WTF", "add", "testFile","rutgers.txt");
    sleep(5);
    execlp("./WTF", "add", "testFile", "testFile/rutgers.txt");
    sleep(5);
    execlp("./WTF", "commit", "testFile");
    sleep(5);
    execlp("./WTF", "push", "testFile");
    sleep(5);
    execlp("./WTF", "remove", "testFile", "cs.txt");
    sleep(5);
    execlp("./WTF", "update", "testFile");
    sleep(5);
    execlp("./WTF", "upgrade", "testFile");
    sleep(5);
    execlp("./WTF", "currentversion", "testFile");
    sleep(5);
    execlp("./WTF", "rollback", "testFile", "1");
    sleep(5);
    execlp("./WTF", "history", "testFile");
    sleep(5);
  }
}
