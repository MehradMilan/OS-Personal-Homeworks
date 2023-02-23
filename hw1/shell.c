#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>

#define FALSE 0
#define TRUE 1
#define INPUT_STRING_SIZE 80
#define ARRAY_SIZE 20
#define PATH_SIZE 256

#include "io.h"
#include "parse.h"
#include "process.h"
#include "shell.h"

int cmd_quit(tok_t arg[]) {
  printf("Bye\n");
  exit(0);
  return 1;
}

int cmd_help(tok_t arg[]);

int cmd_pwd(tok_t arg[]);
int cmd_cd(tok_t arg[]);
int cmd_wait(tok_t arg[]);

/* Command Lookup table */
typedef int cmd_fun_t (tok_t args[]); /* cmd functions take token array and return int */
typedef struct fun_desc {
  cmd_fun_t *fun;
  char *cmd;
  char *doc;
} fun_desc_t;

fun_desc_t cmd_table[] = {
  {cmd_help, "?", "show this help menu"},
  {cmd_quit, "quit", "quit the command shell"},
  {cmd_pwd, "pwd", "print working directory"},
  {cmd_cd, "cd", "change working directory"},
  {cmd_wait, "wait", "wait for background process"}
};

int cmd_help(tok_t arg[]) {
  int i;
  for (i=0; i < (sizeof(cmd_table)/sizeof(fun_desc_t)); i++) {
    printf("%s - %s\n",cmd_table[i].cmd, cmd_table[i].doc);
  }
  return 1;
}

int cmd_pwd(tok_t arg[]) {
  char cwd[256];
  if(getcwd(cwd, sizeof(cwd)) == NULL)
    return -1;
  else {
    printf("%s\n", cwd);
    return 1;
  }
}

int cmd_cd(tok_t arg[]) {
  char *addr = (char *)malloc(sizeof(arg[0]));
  strcpy(addr, arg[0]);
  if(chdir(addr) != 0) {
    printf("cd: %s: No such file or directory\n", addr);
    return -1;
  }
  return 1;
}

int cmd_wait(tok_t arg[]) {
  pid_t pid;
  int status;
  pid = waitpid(-1, &status, 0);
  return 1;
}

int lookup(char cmd[]) {
  int i;
  for (i=0; i < (sizeof(cmd_table)/sizeof(fun_desc_t)); i++) {
    if (cmd && (strcmp(cmd_table[i].cmd, cmd) == 0)) return i;
  }
  return -1;
}

void set_signal_state(int STATE) {
  signal(SIGINT, STATE);
  signal(SIGQUIT, STATE);
  signal(SIGTSTP, STATE);
  signal(SIGTTIN, STATE);
  signal(SIGTTOU, STATE);
}

void init_shell()
{
  /* Check if we are running interactively */
  shell_terminal = STDIN_FILENO;

  /** Note that we cannot take control of the terminal if the shell
      is not interactive */
  shell_is_interactive = isatty(shell_terminal);

  if(shell_is_interactive){

    /* force into foreground */
    while(tcgetpgrp (shell_terminal) != (shell_pgid = getpgrp()))
      kill( - shell_pgid, SIGTTIN);

    shell_pgid = getpid();
    /* Put shell in its own process group */
    if(setpgid(shell_pgid, shell_pgid) < 0){
      perror("Couldn't put the shell in its own process group");
      exit(1);
    }

    /* Take control of the terminal */
    tcsetpgrp(shell_terminal, shell_pgid);
    tcgetattr(shell_terminal, &shell_tmodes);

    set_signal_state(SIG_IGN);
    // ignore_signals();
  }
  /** YOUR CODE HERE */
}

/**
  * Add a process to our process list
  */
void add_process(process *proc) {

}

/**
  * Creates a process given the inputString from stdin
  */
process* create_process(char* inputString)
{
  process *p =  (process *) malloc(sizeof(process));
  p->stopped = 0;
  p->completed = "";
  // p->background = "";
  p->stdin = STDIN_FILENO;
  p->stdout = STDOUT_FILENO;
  p->stderr = STDERR_FILENO;
  p->next = NULL;
  p->prev = NULL;
  tok_t *t;
  t = getToks(inputString);
  p->argv = t;
  int i = 0;
  while (t[i])
    i++;
  p->argc = i;
  int b = is_background_process(p);
  if (b > 0)
    p->background = 1;
  else
    p->background = 0;
  return p;
  /** YOUR CODE HERE */
}

int set_input_redirect(process *p, int index) {
  tok_t *t = p->argv;
  int f_open = open(t[index+1], O_RDONLY);
  if (f_open < 0) {
    perror("File Error");
    return -1;
  }
  else {
    p->stdin = f_open;
    t[index] = NULL;
  }
  return 0;
}

int set_output_redirect(process *p, int index) {
  tok_t *t = p->argv;
  int f_open = open(t[index+1], O_WRONLY | O_CREAT | O_TRUNC, 0777);
  if (f_open < 0) {
    perror("File Error");
    return -1;
  }
  else {
    p->stdout = f_open;
    t[index] = NULL;
  }
  return 0;
}

int redirect_io(process *p) {
  int flag = 0;
  int dir_index = isDirectTok(p->argv, "<");
  if(dir_index != 0)
    flag = set_input_redirect(p, dir_index);
  dir_index = isDirectTok(p->argv, ">");
  if(dir_index != 0)
    flag = set_output_redirect(p, dir_index);
  return flag;
}

int is_background_process(process *p) {
  tok_t *t = p->argv;
  int index = isDirectTok(t, "&");
  if(index > 0)
    p->argv[index] = NULL;
  return index;
}

char *get_exec_path(char *inputString) {
  if (inputString[0] == '/' || inputString[0] == '.') {
    char *path = malloc(INPUT_STRING_SIZE+1);
    strcpy(path, inputString);
    return path;
  }
  else {
    char *x = malloc(PATH_SIZE);
    x = getenv("PATH");
    char *path = malloc(PATH_SIZE);
    strcpy(path, x);
    char *dir[ARRAY_SIZE];
    int l = extract_PATH(path, dir);
    for (int i = 0; i < l; i++) {
      char *filename = malloc(PATH_SIZE);
      strcpy(filename, dir[i]);
      char *command = malloc(PATH_SIZE);
      strcpy(command, inputString);
      tok_t *t;
      t = getToks(command);
      char *p = t[0];
      strcpy(filename, dir[i]);
      strcat(filename, "/");
      strcat(filename, p);
      if(access(filename, F_OK) == 0) {
        strcpy(filename, dir[i]);
        strcat(filename, "/");   
        strcat(filename, inputString);
        return filename;
      }
    }
  }
  return NULL;
}

int extract_PATH(char *path, char **dir) {
  int i = 0;
  char *p = strtok(path, ":");
  while(p != NULL) {
    dir[i++] = p;
    p = strtok(NULL, ":");
  }
  return i;
}

int shell (int argc, char *argv[]) {
  char *s = malloc(INPUT_STRING_SIZE+1);			/* user input string */
  tok_t *t;			/* tokens parsed from input */
  int lineNum = 0;
  int fundex = -1;
  pid_t pid = getpid();		/* get current processes PID */
  pid_t ppid = getppid();	/* get parents PID */
  pid_t cpid, tcpid, cpgid;

  init_shell();

  // printf("%s running as PID %d under %d\n",argv[0],pid,ppid);

  lineNum=0;
  // fprintf(stdout, "%d: ", lineNum);
  while ((s = freadln(stdin))) {
    if(strcmp(s, "\n")==0)
      continue;
    char *inputString = malloc(INPUT_STRING_SIZE+1);
    strcpy(inputString, s);
    t = getToks(s); /* break the line into tokens */
    fundex = lookup(t[0]); /* Is first token a shell literal */
    if(fundex >= 0) cmd_table[fundex].fun(&t[1]);
    else {
      char *path = malloc(INPUT_STRING_SIZE+1);
      path = get_exec_path(inputString);
      process *p = create_process(path);
      int flag = redirect_io(p);
      pid_t npid = fork();
      if (npid == 0) {
        // setpgrp();
        // default_signals();
        set_signal_state(SIG_DFL);
        p->pid = getpid();
        launch_process(p);
      } else if (npid > 0) {
        p->pid = npid;
        if(!p->background) {
          setpgid(npid, npid);
          tcsetpgrp(shell_terminal, npid);
          // wait(NULL);
          waitpid(p->pid, &p->status, WUNTRACED);
          tcsetpgrp(shell_terminal, shell_pgid);
          // put_process_in_foreground(p, 0);
        }
      }
      // free(path);
      // free(inputString);
    }
    // fprintf(stdout, "%d: ", lineNum);
  }
  return 0;
}
