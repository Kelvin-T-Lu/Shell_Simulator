/* This is the only file you should update and submit. */

/* Fill in your Name and GNumber in the following two comment fields
 * Name: Kelvin Lu
 * GNumber: G01194210
 */

#include "shell.h"
#include "parse.h"

/* Constants */
#define DEBUG 0
#define RUNNING 1
#define STOPPED 0
#define TERMINATED -1 
//Linked List Node of Processes
typedef struct Node{
   int JOBID;
   int state;  
   pid_t pid; 
   char *cmd;  
   //Cmd_aux *aux; 
   struct Node *next; 
}Node;

static int node_size = 0; 
 
static const char *shell_path[] = { "./", "/usr/bin/", NULL };
static const char *built_ins[] = { "quit", "help", "kill", 
  "fg", "bg", "jobs", NULL};

static pid_t prevPID = 0; //Previous Foreground PID termination 
static pid_t currPID;//Current Foreground PID 
static pid_t fgPID; //Foreground PID
static Node *fgProcess;  
static Node *head = NULL; 

void removeForegroundProcess();

int checkBuiltIn(char *argv);
int isBackground(Cmd_aux *aux); 
void replaceExitCode(char *argv[], pid_t exitCode); 

void executeBuiltIn(char *cmd, char *argv[]); 
void executeNonBuiltIn(char *cmd, char *argv[], Cmd_aux *aux); 
void executeIO(char *cmd, char *argv[], Cmd_aux *aux, char *path1, char* path2);
 
Node *init_Node(char *, pid_t, int state);
void insertNode(char *, pid_t, int state);
//void insertNode(char *, Cmd_aux *aux, pid_t);
Node *removeNodeJob(int jobID);  
Node *removeNodePID(pid_t element);
Node *findPID(int jobID); 
Node *findNode(pid_t pid);  
void deleteNode(Node *); 
void freeLinkedList(Node *); 
/* The entry of your shell program */

void child_handler(int sig){
  if(sig == 2){//Interupt Ctrl + c
    log_ctrl_c();
    if(!fgProcess){
      return; 
    } 
    //printf("fgPID: %d\n", fgPID);  
    kill(fgPID, 2);  
  }else if(sig == 20){ //Stop Ctrl + Z  
    log_ctrl_z();
    if(!fgProcess){ 
      return; 
    }
    kill(fgPID, 20);
    insertNode(fgProcess->cmd, fgProcess->pid, STOPPED);
    removeForegroundProcess();
  }else if(sig == 17){ //SigChld 
    int childstatus; 
    int tempPID = waitpid(-1, &childstatus, WUNTRACED|WCONTINUED);
    //printf("tempPID %d\n", tempPID); 
    if(tempPID <= 0){ //Error
      //exit(1);
      return;  
    } 
    if(WIFEXITED(childstatus)){ //Normal Termination
      if(tempPID == fgPID){ //Foreground Process Termination
        prevPID = WEXITSTATUS(childstatus);
        log_job_state(fgPID, LOG_FG, fgProcess->cmd, LOG_TERM);
        removeForegroundProcess();
      }else{ //Background Process Termination
        Node *node = removeNodePID(tempPID); 
        log_job_state(tempPID, LOG_BG, node->cmd, LOG_TERM); 
        deleteNode(node); 
      }
    }else if(WIFSIGNALED(childstatus)){ //Terminated by Signal 
      if(tempPID == fgPID){ //Foreground Process Termination
        log_job_state(fgPID, LOG_FG, fgProcess->cmd, LOG_TERM_SIG);
        removeForegroundProcess();
      }else{ //Background Process Termination
        Node *node = removeNodePID(tempPID); 
        log_job_state(tempPID, LOG_BG, node->cmd, LOG_TERM_SIG);
        deleteNode(node);  
      }
    }else if(WIFSTOPPED(childstatus)){
      //printf("tempPID: %d\n", tempPID); 
      if(tempPID == fgPID){//FG stopped
        insertNode(fgProcess->cmd, fgPID, STOPPED);
        log_job_state(fgPID, LOG_FG, fgProcess->cmd, LOG_STOP);
        removeForegroundProcess();
      }else{//BG Stopped. 
        Node *node = findNode(tempPID); 
        node->state = STOPPED;
        log_job_state(tempPID, LOG_BG, node->cmd, LOG_STOP);
        return;   
      } 
    }else if(WIFCONTINUED(childstatus)){//Background Node Continue
      if(tempPID == fgPID){ //fg command
        log_job_state(tempPID, LOG_BG, fgProcess->cmd, LOG_CONT); 
      }else{
        Node *node = findNode(tempPID); 
        log_job_state(tempPID, LOG_BG, node->cmd, LOG_CONT);  
      }
    }
  }
}
//Resets the global static of foreground processes.  
void removeForegroundProcess(){
  fgPID = 0;  
  deleteNode(fgProcess); 
  fgProcess = NULL;       
}
int main() {
  struct sigaction act; 
  memset(&act, 0, sizeof(struct sigaction)); 
  act.sa_handler = child_handler; 
  sigaction(SIGCHLD, &act, NULL); 
  sigaction(SIGINT, &act, NULL); 
  sigaction(SIGTSTP, &act, NULL); 
   
  
  char cmdline[MAXLINE];        /* Command line */
  char *cmd = NULL;

  /* Intial Prompt and Welcome */
  log_prompt();
  log_help();

  /* Shell looping here to accept user command and execute */
  while (1) {
    char *argv[MAXARGS];        /* Argument list */
    Cmd_aux aux;                /* Auxilliary cmd info: check parse.h */
    /* Print prompt */
    log_prompt();
    
    //printf("PrevPID: %d \n", prevPID); 
    /* Read a line */
    // note: fgets will keep the ending '\n'
    if (fgets(cmdline, MAXLINE, stdin) == NULL) {
      if (errno == EINTR)
        continue;
      exit(-1);
    }

    if (feof(stdin)) {  /* ctrl-d will exit shell */
      exit(0);
    }

    /* Parse command line */
    if (strlen(cmdline)==1)   /* empty cmd line will be ignored */
      continue;     

    cmdline[strlen(cmdline) - 1] = '\0';        /* remove trailing '\n' */

    cmd = malloc(strlen(cmdline) + 1);
    snprintf(cmd, strlen(cmdline) + 1, "%s", cmdline);

    /* Bail if command is only whitespace */
    if(!is_whitespace(cmd)) {
      initialize_argv(argv);    /* initialize arg lists and aux */
      initialize_aux(&aux);
      parse(cmd, argv, &aux); /* call provided parse() */

      if (DEBUG){  /* display parse result, redefine DEBUG to turn it off */
        debug_print_parse(cmd, argv, &aux, "main (after parse)");
      }
      //int childstatus;  
       
      replaceExitCode(argv, prevPID); 
      /* After parsing: your code to continue from here */
      //Executes Built In Function, if applicable 
      if(checkBuiltIn(argv[0])){
        executeBuiltIn(cmd, argv);  
      }else{ //ASSUMPTION: Process is not Built In
        //Blocks the Child Signal until list addition.
        sigset_t mask, save; 
        sigemptyset(&mask); 
        sigaddset(&mask, SIGCHLD); 
        sigprocmask(SIG_BLOCK, &mask, &save); 
       
        //Create Child
        currPID = fork(); 
        if(currPID==0){
          currPID = getpid();
          sigprocmask(SIG_SETMASK, &save, NULL); 
          setpgid(0, 0); 
          executeNonBuiltIn(cmd, argv,&aux); 
        }else{
          if(isBackground(&aux)){
            log_start(currPID, LOG_BG, cmd); 
            insertNode(cmd, currPID, RUNNING); //Insert into LinkedList.
          }else{//Foreground Process Termination
            fgPID = currPID; 
            log_start(fgPID, LOG_FG, cmd);
            fgProcess = init_Node(cmd, fgPID, RUNNING);            
          } 
          sigprocmask(SIG_SETMASK, &save, NULL);      
        }
      }
      while(fgPID){//Waits for FG if applicable. 
         pause(); 
      }
      /*if(fgProcess){
       int status;
       waitpid(fgPID, &status,0); 
      } */ 
    }

    free_options(&cmd, argv, &aux); 
  }

  free(head);
  deleteNode(fgProcess);   
  return 0;
}

/**
 * Intialize a node into the Linked List. 
 */ 
Node *init_Node(char *cmd, pid_t inputPID, int state){
  Node *node = malloc(sizeof(Node)); 
  
  node->JOBID = 0; 
  node->state = state; 
  node-> pid = inputPID;  
  //node->aux= NULL; 
  node->next= NULL;
  node->cmd = malloc(MAXLINE * sizeof(char)); 
  strcpy(node->cmd, cmd); 
  return node;  
}

/**
 * Insert the node into the Linked List. 
 * Return: The head of the Node.
 */ 
void insertNode(char *cmdInput, pid_t elementPID, int state){
  //Creates memory for the node. 
  //Intialize its variable. 
  //Check if header is empty. 
  // If Empty, creates a new head. 
  //If not, insert Node to the end. 

  //Creates an insertion node. 
  Node *insert_node = init_Node(cmdInput, elementPID, state); 
  //printf("Insert Node Pointer %p\n", insert_node); 	
  if(!head){
    //printf("Head insertion\n"); 
    insert_node->JOBID = 1; 
    head = insert_node;
    node_size = 1;  
    //printf("Node's String: %s\n", insert_node->cmd);
  }else{
    //printf("Node Insertion\n"); 
    Node *curr = head; 
    while(curr->next != NULL){ //Gets the last Node in the list. 
      curr = (curr->next); 
    }
    (insert_node -> JOBID) = (curr->JOBID)+1; 
    curr->next = insert_node;
    node_size++;     
  }
}
/**
 * Remove a node from the Linked List. 
 * Return: Element of Remove Node if sucessful, NULL Otherwise.   
 */
Node *removeNodeJob(int jobID){
  //Checks Head Node. 
  //Iterates through the Node. 
  //Removes the Node.
  
  //ASSUMPTION: Empty List 
  if(node_size == 0){
    return NULL;  
  }  
  Node *element = head; 
  //ASSUMPTION: 1 Node in List
  if(node_size == 1){ 
    if(head->JOBID == jobID){
      node_size = 0; 
      head = NULL;
      fgPID = 0; 
      fgProcess = NULL;
      return element;  
    }else{
      return NULL; 
    }
  }

  //ASSUMPTION: > 1 Node in list

  //Checks the header Node. 
  if(head->JOBID == jobID){
    node_size--; 
    head = head->next;
    return element;  
  }
  
  //Iterating through List, Curr is the Second Element. 
  Node *curr = head->next; //Current Node Iteration.  
  Node *prev = head; //Previous Node of Current Head. 
  do{
    if(curr->JOBID == jobID){ //Node is found. 
      node_size--;
      element = curr; 
      (prev->next) = curr->next; 
      return element; 
    }
    prev = curr; 
    curr = curr->next; 
  }while(curr !=NULL);  
  return NULL; 
} 
/**
 * Remove a node from the Linked List. 
 * Return: Element of Remove Node if sucessful, NULL Otherwise.   
 */
Node *removeNodePID(pid_t input){
  //Checks Head Node. 
  //Iterates through the Node. 
  //Removes the Node.
  
  //ASSUMPTION: Empty List 
  if(node_size == 0){
    return NULL;  
  }  
  Node *element = head; 
  //ASSUMPTION: 1 Node in List
  if(node_size == 1){ 
    if(head->pid == input){
      node_size = 0; 
      head = NULL;
      fgPID = 0; 
      fgProcess = NULL;
      return element;  
    }else{
      return NULL; 
    }
  }

  //ASSUMPTION: > 1 Node in list

  //Checks the header Node. 
  if(head->pid == input){
    node_size--; 
    head = head->next;
    return element;  
  }
  
  //Iterating through List, Curr is the Second Element. 
  Node *curr = head->next; //Current Node Iteration.  
  Node *prev = head; //Previous Node of Current Head. 
  do{
    if(curr->pid == input){ //Node is found. 
      node_size--;
      element = curr; 
      (prev->next) = curr->next; 
      return element; 
    }
    prev = curr; 
    curr = curr->next; 
  }while(curr !=NULL); 
  
  return NULL; 
}
/**
 * Return: Node with Job ID, Null Otherwise
 */ 
Node *findPID(int jobID){
  Node *curr = head; 
  while(curr != NULL){
    //printf("JobID: %d Cmd: %s \n", curr->JOBID, curr->cmd);
    if(curr->JOBID == jobID){
      return curr; 
    } 
    curr = (curr->next); 
  } 
  return NULL;   
} 
/**
 * Return: Node with pid, Null Otherwise
 */
Node *findNode(pid_t pid){
  Node *curr = head; 
  while(curr != NULL){
    //printf("JobID: %d Cmd: %s \n", curr->JOBID, curr->cmd);
    if(curr->pid == pid){
      return curr; 
    } 
    curr = (curr->next); 
  } 
  return NULL;   
} 
/**
 * Free each node of the Linked list
 */
void freeLinkedList(Node *node){
 if(!node){ return; }

 freeLinkedList(node->next); 
 
 deleteNode(node); 
}

/**
 * Frees the data within a node
 */
void deleteNode(Node *node){
  if(node){  
    free(node->cmd); 
    free(node); 
  }
}
/**
 * Replaces any $? with exitCode. 
 */ 
void replaceExitCode(char *argv[], pid_t exitCode){
   int i = 0; 
   char *code = malloc(10 * sizeof(char)); 
   sprintf(code, "%d", exitCode); 
   while(argv[i] != NULL){
     if(strcmp(argv[i], "$?")==0) {
        strcpy(argv[i],code); 
        printf("Argv[i] = %s\n", argv[i]); 
        log_replace(i, code);  
     }
     i++;   
   }
   free(code); 
} 
/**
 * Return 1 if process is a background process, 0 otherwise. 
 */
int isBackground(Cmd_aux *aux){
  if(aux->is_bg ==1){
    return 1; 
  } 
  return 0;
} 

/**
 * Checks whether a command is built in. Return 1 if built in, 0 otherwise. 
 */
int checkBuiltIn(char *cmd){
  int i = 0; 
  while(built_ins[i] != NULL){
    if(strcmp(cmd, built_ins[i]) == 0){
       return 1; 
    }
    i++; 
  }
  return 0; 
}
/**
 * Executes a built in command, if applicable.  
 */
void executeBuiltIn(char *cmd, char *argv[]){ 
  //Compares the first argv to the built_in commands. 
  //   If it equals, execute respective commands. 
 
  if(strcmp(*argv, *(built_ins+0))==0){//Quit Command
    //printf("quit"); 
    log_quit();  
    exit(0); 
  }else if(strcmp(*argv, *(built_ins+1))==0){//Help Command Execution
    //printf("help"); 
    log_help(); 
  }else if(strcmp(*argv, *(built_ins+2))==0){ //Kill Command Execution 
    //printf("kill");
    int signalNumber = atoi(argv[1]); 
    int pid = atoi(argv[2]); 
    //printf("%d", signalNumber);
    if(kill(pid, signalNumber)==-1){//Error
      return; 
    }
    log_kill(signalNumber, pid); 
    Node *node = findNode(pid);
    if(node){ //If node is in list, update node's state. 
      switch(signalNumber){
        case 20: 
          node->state = STOPPED; //SigStp
          break; 
        case 18: 
          node->state = RUNNING; //SigCnt
          break; 
      }
    }  
    
  }else if(strcmp(*argv, *(built_ins+3))==0){ //fg Command Execution //Test Required
    //printf("fg");
    int jobID = atoi(argv[1]); 
    //fgProcess = findPID(jobID); //Finds the Node in the Linked List  
    fgProcess = removeNodeJob(jobID); //Finds the Node in the Linked List 
    if(!fgProcess){//Error: Job ID not in list. 
      log_jobid_error(jobID); 
      return; 
    }
    fgPID = fgProcess->pid;
    kill(fgPID, 18); 
    log_job_move(fgPID, LOG_FG, fgProcess->cmd);
    /*int childstatus; //Wait for FG to finish Two Forks 
    waitpid(fgPID, &childstatus, 0);
    prevPID = WEXITSTATUS(childstatus);
    log_job_state(fgPID, LOG_FG, fgProcess->cmd, LOG_TERM);
    removeForegroundProcess();*/ 
  }else if(strcmp(*argv, *(built_ins+4))==0){ //bg Command Execution 
    //printf("bg"); 
    int jobID = atoi(argv[1]); 
    Node *node = findPID(jobID); 
    if(!node){ //JobID Error
      log_jobid_error(jobID); 
      return; 
    }
    kill(node->pid, 18); 
    log_job_move(node->pid, LOG_BG, node->cmd); 
  }else if(strcmp(*argv, *(built_ins+5))==0){ //jobs Command execution  
    //printf("jobs");
    log_job_number(node_size);
    Node *curr = head; 
    while(curr != NULL){
      //printf("JobID: %d Cmd: %s \n", curr->JOBID, curr->cmd);
      int pid = curr->pid;
      switch(curr->state){
        case RUNNING: 
         log_job_details(curr->JOBID, pid, "Running", curr->cmd); 
         break; 
        case STOPPED: 
         log_job_details(curr->JOBID, pid, "Stopped", curr->cmd);
         break; 
      }
      /*if(curr->state == RUNNING){ //Log Each BG State's
        log_job_details(curr->JOBID, pid, "Running" ,curr->cmd); 
      }else if(curr->state == STOPPED){
        log_job_details(curr->JOBID, pid, "Stopped", curr->cmd); 
      }*/ 
      curr = (curr->next);  
    } 
  } 
}

/**
 * Executes a non-built in command. 
 */
void executeNonBuiltIn(char *cmd, char *argv[], Cmd_aux *aux){
  //Forms the two possible pathways in the shell.
  //Redirect Files from input and output files, if applicable.  
  //Attempts to execute both pathways, 
  //   IF both fails, return an error.  

  //Creates Pathways
  char path1[50]; // ./ 
  char path2[50];// usr/bin/
  strcpy(path1, *(shell_path+0)); 
  strcat(path1, argv[0]); //path1 = ./argv[0]
  strcpy(path2, *(shell_path+1)); 
  strcat(path2, argv[0]); //path2 = usr/bin/argv[0]
  //printf("Path 1: %s\n", path1); 
  //printf("Path 2: %s\n", path2); 

  //File Redirections
  int fdin = -1; 

  if(aux->in_file != NULL){ //< 
    //printf("input activated\n");
    //printf("%s\n", aux->in_file);
    fdin = open(aux->in_file, O_RDONLY, 0600); 
    if(fdin < 0){
      //printf("Error activated.\n"); 
      log_file_open_error(aux->in_file); 
      exit(1); 
    }else{
      //printf("fdin: %d\n", fdin);  
      dup2(fdin, STDIN_FILENO); 
      close(fdin); 
    }
  }

  int fdout = -1; 
  if(aux->out_file != NULL){
    //printf("output activated\n");
    if(aux->is_append==1){ //>>
      fdout = open(aux->out_file, O_WRONLY|O_CREAT|O_APPEND, 0600);
    } else{//> 
      fdout=open(aux->out_file, O_WRONLY|O_CREAT|O_TRUNC, 0600); 
    }
    if(fdout < 0){
        log_file_open_error(aux->out_file); 
        exit(1); 
    }
    dup2(fdout, STDOUT_FILENO); 
    close(fdout);
  } 
  
  //Executes Pathways
  if(execv(path1, argv)==-1){ //path1 
    //printf("Path 1 attempted\n"); 
    if(execv(path2, argv)==-1){ //path2
      //Error
      log_command_error(cmd);
      exit(1); 
     }
    //printf("After Path2\n"); 
    return; 
  }
}
