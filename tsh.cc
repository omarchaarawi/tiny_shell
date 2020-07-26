// 
// tsh - A tiny shell program with job control
// 
// Omar Chaarawi
// omarchaarawi
//

using namespace std;

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <string>

#include "globals.h"
#include "jobs.h"
#include "helper-routines.h"

//
// Needed global variable definitions
//

static char prompt[] = "tsh> ";
int verbose = 0;

//
// You need to implement the functions eval, builtin_cmd, do_bgfg,
// waitfg, sigchld_handler, sigstp_handler, sigint_handler
//
// The code below provides the "prototypes" for those functions
// so that earlier code can refer to them. You need to fill in the
// function bodies below.
// 

void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);
int count_pipes(char *cmdline);  // counts number of pipes to create

//
// main - The shell's main routine 
//
int main(int argc, char *argv[]) 
{
  int emit_prompt = 1; // emit prompt (default)

  //
  // Redirect stderr to stdout (so that driver will get all output
  // on the pipe connected to stdout)
  //
  dup2(1, 2);

  /* Parse the command line */
  char c;
  while ((c = getopt(argc, argv, "hvp")) != EOF) {
    switch (c) {
    case 'h':             // print help message
      usage();
      break;
    case 'v':             // emit additional diagnostic info
      verbose = 1;
      break;
    case 'p':             // don't print a prompt
      emit_prompt = 0;  // handy for automatic testing
      break;
    default:
      usage();
    }
  }

  //
  // Install the signal handlers
  //
  // These are the ones you will need to implement
  //
  Signal(SIGINT,  sigint_handler);   // ctrl-c
  Signal(SIGTSTP, sigtstp_handler);  // ctrl-z
  Signal(SIGCHLD, sigchld_handler);  // Terminated or stopped child

  //
  // This one provides a clean way to kill the shell
  //
  Signal(SIGQUIT, sigquit_handler); 

  //
  // Initialize the job list
  //
  initjobs(jobs);

  //
  // Execute the shell's read/eval loop
  //
  for(;;) {
    //
    // Read command line
    //
    if (emit_prompt) {
      printf("%s", prompt);
      fflush(stdout);
    }

    char cmdline[MAXLINE];

    if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin)) {
      app_error("fgets error");
    }
    //
    // End of file? (did user type ctrl-d?)
    //
    if (feof(stdin)) {
      fflush(stdout);
      exit(0);
    }

    //
    // Evaluate command line
    //
    eval(cmdline);
    fflush(stdout);
    fflush(stdout);
  } 

  exit(0); //control never reaches here
}
  
/////////////////////////////////////////////////////////////////////////////
//
// eval - Evaluate the command line that the user has just typed in
// 
// If the user has requested a built-in command (quit, jobs, bg or fg)
// then execute it immediately. Otherwise, fork a child process and
// run the job in the context of the child. If the job is running in
// the foreground, wait for it to terminate and then return.  Note:
// each child process must have a unique process group ID so that our
// background children don't receive SIGINT (SIGTSTP) from the kernel
// when we type ctrl-c (ctrl-z) at the keyboard.
//
/*
int count_pipes(string cmdline){
  int counter = 0;
  for(int i = 0; i < cmdline.length(); i++ ){
    if(cmdline[i] == '|')
      counter++;
  }
  return counter;
}
*/

void eval(char *cmdline) 
{
  char *argv[MAXARGS];
  struct job_t *job;  // initialize job struct
  sigset_t set;       // initialize mask
  sigemptyset(&set);  // empty set
  
  sigaddset(&set, SIGCHLD);  // add sigchld to set
  sigaddset(&set, SIGINT);   // add sigint to set
  sigaddset(&set, SIGTSTP);  // add tstp to set
  
  int bg = parseline(cmdline, argv); // bg is true if background job, false if foreground
  if (argv[0] == NULL)  
    return;   /* ignore empty lines */
  
  pid_t pid;  // Initialize pid
  if (!builtin_cmd(argv)){
    sigprocmask(SIG_BLOCK, &set, NULL);  // block signals
    
    // if not a builtin command then
    // fork and execv and a few other things
    pid = fork();
    
    if(pid < 0){  // make sure fork executed correctly
      printf("Error creating fork.\n");
      return;
    }
    setpgrp(); // set the process group id
    if(pid == 0){   // if child, start a fork and then execute job in the child process
        
        sigprocmask(SIG_UNBLOCK, &set, NULL);       // unblock signals before exec job
        int checker = execve(argv[0], argv, NULL);  // execute job and set checker to return value
        if(checker < 0){  // error check to make sure execve executed
          printf("%s: Command not found\n", argv[0]);
          exit(0);
        }
      
    }
    else{  // parent process
      if(!bg){  // if not background job
        
        addjob(jobs, pid, FG, cmdline); // add job
        sigprocmask(SIG_UNBLOCK, &set, NULL);  // unblock signals after adding a job
        waitfg(pid); // wait for child and reap it
      } 
      else{ // foreground job
        addjob(jobs, pid, BG, cmdline);
        sigprocmask(SIG_UNBLOCK, &set, NULL);  // unblock signals after adding a job

        job = getjobpid(jobs, pid);
        printf("[%d] (%d) %s", job->jid, pid, cmdline);
      }
    }
  } 
  return;
}


/////////////////////////////////////////////////////////////////////////////
//
// builtin_cmd - If the user has typed a built-in command then execute
// it immediately. The command name would be in argv[0] and
// is a C string. We've cast this to a C++ string type to simplify
// string comparisons; however, the do_bgfg routine will need 
// to use the argv array as well to look for a job number.
//
int builtin_cmd(char **argv) 
{
  // pid_t pid = getpid();
  //pid_t pid = fgpid(jobs);fclear
  string cmd(argv[0]);
  if(cmd == "quit"){
    exit(0);
  }
  else if(cmd == "jobs"){
    listjobs(jobs);
    return 1;
  }
  else if((cmd == "bg" || cmd == "fg")){
    do_bgfg(argv);
    return 1;
  }
  /* Tried to implement clear builtin command
  else if((cmd == "clear")){
    kill(pid,12);
    return 1;
  }
  */
  return 0;     /* not a builtin command */
}

/////////////////////////////////////////////////////////////////////////////
//
// do_bgfg - Execute the builtin bg and fg commands
//
void do_bgfg(char **argv) 
{
  struct job_t *jobp = NULL;
    
  /* Ignore command if no argument */
  if (argv[1] == NULL) {
    printf("%s command requires PID or %%jobid argument\n", argv[0]);
    return;
  }
    
  /* Parse the required PID or %JID arg */
  if (isdigit(argv[1][0])) {
    pid_t pid = atoi(argv[1]);
    if (!(jobp = getjobpid(jobs, pid))) {
      printf("(%d): No such process\n", pid);
      return;
    }
  }
  else if (argv[1][0] == '%') {
    int jid = atoi(&argv[1][1]);
    if (!(jobp = getjobjid(jobs, jid))) {
      printf("%s: No such job\n", argv[1]);
      return;
    }
  }	    
  else {
    printf("%s: argument must be a PID or %%jobid\n", argv[0]);
    return;
  }

  // MY CODE STARTS HERE:
  string cmd(argv[0]);  // cast argv[0] as a string so we can compare to bg and fg
  
  if(cmd == "fg"){  // if FG
    jobp->state = FG;  // change job state to FG
    kill(-jobp->pid, SIGCONT);  //rerun job in FG mode now
    waitfg(jobp->pid);  // added so that only one process runs in foreground at a time
  }
  else if(cmd == "bg"){
    jobp->state = BG; // change job state
    kill(-jobp->pid, SIGCONT);  // rerun job but in bg mode now
    printf("[%d] (%d) %s", jobp->jid, jobp->pid, jobp->cmdline);  // return to user job info
  }
  return;
}

/////////////////////////////////////////////////////////////////////////////
//
// waitfg - Block until process pid is no longer the foreground process
//
void waitfg(pid_t pid){
  while(true){
    if(pid != fgpid(jobs)) // if process is not a foreground process return
      return;
    else 
      sleep(1);   // else sleep for 1 second and wait
  }
}

/////////////////////////////////////////////////////////////////////////////
//
// Signal handlers
//


/////////////////////////////////////////////////////////////////////////////
//
// sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
//     a child job terminates (becomes a zombie), or stops because it
//     received a SIGSTOP or SIGTSTP signal. The handler reaps all
//     available zombie children, but doesn't wait for any other
//     currently running children to terminate.  
//
void sigchld_handler(int sig) 
{
  int status;  // initialize status
  pid_t pid;  // initialize pid
  
  // wait for any child process, but return if no children exited (WNOHANG)
  // or return if children process has stopped (WUNTRACED)
  pid = waitpid(-1, &status, WNOHANG | WUNTRACED);  
  
  while(pid > 0){  // waitpid() returns pid of child that changed state, returns 0
                   // returns 0 if no state change
    
    if(WIFSIGNALED(status)){  // if process was terminated by ctrl-c
      int job_id = getjobpid(jobs,pid)->jid;
      printf("Job [%d] (%d) terminated by signal 2\n", job_id, pid);  // report to user that job was terminated
      deletejob(jobs,pid);
    }
    else if(WIFSTOPPED(status)){  // if child process was stopped
      struct job_t *job = getjobpid(jobs, pid);
      job->state = ST;  // change job state
      printf("Job [%d] (%d) stopped by signal 20\n", job->jid, pid);  // report to user that job was stopped
      return;
    }
    else if (WIFEXITED(status))  // if process exited normally
      deletejob(jobs,pid);
  pid = waitpid(-1, &status, WNOHANG | WUNTRACED);  // check for more children to sort
  }
  return;
}

/////////////////////////////////////////////////////////////////////////////
//
// sigint_handler - The kernel sends a SIGINT to the shell whenver the
//    user types ctrl-c at the keyboard.  Catch it and send it along
//    to the foreground job.  
//
void sigint_handler(int sig) {
  pid_t pid = fgpid(jobs);
  if(pid > 0){  // if running in foreground
    kill(-pid, sig);
  }  
  return;
}

/////////////////////////////////////////////////////////////////////////////
//
// sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
//     the user types ctrl-z at the keyboard. Catch it and suspend the
//     foreground job by sending it a SIGTSTP.  
//
void sigtstp_handler(int sig) 
{
  pid_t pid = fgpid(jobs);
  if(pid > 0){
    kill(-pid, sig);
  }
  return;
}


/*********************
 * End signal handlers
 *********************/




