#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAXCHAR 1035
#define CTRLC_EXIT 0
#define CTRLC_PARENT 1
#define CTRLC_CHILD 2

char **argv, **linesBg;
char *iFileName, *oFileName; // redirection destination file
char *homeDir, *lastDir, *lastDirTmp;
size_t numCmd, numPipe, numBg = 0, argc; // argc inlcudes '|'
int isSQClosed, isDQClosed, isRPEnd, isFirstFgets, hasIOError, hasBg;
int hasIRdrct, hasORdrct; // hasORdrct: 1 for '>', 2 for '>>'
int iFd, oFd;
pid_t *pidBgArr;

void inputParamInitialize() {
  isSQClosed = 1;
  isDQClosed = 1;
  isRPEnd = 1;
  isFirstFgets = 1;
  hasIOError = 0;
  hasBg = 0;
}

void execParamInitialize() {
  numCmd = 0;
  numPipe = 0;
  argc = 0;
  hasIRdrct = 0;
  hasORdrct = 0;
  iFd = 0;
  oFd = 1;
}

void freeArgv() {
  for (size_t i = 0; i < MAXCHAR; ++i)
    free(argv[i]);
  free(argv);
}

void freeOuter() {
  free(iFileName);
  free(oFileName);
  free(lastDir);
  free(lastDirTmp);
  for (size_t i = 0; i < numBg; ++i)
    free(linesBg[i]);
  free(linesBg);
  free(pidBgArr);
}

struct sigaction mySigAction;
int ctrlCStatus;
void sigHandler() {
  // parent and child receive ctrl+c at the same time
  // only parent is responsible for printing '\n'
  // child exits immediately
  if (ctrlCStatus == CTRLC_PARENT) {
    printf("\n");
    ctrlCStatus = CTRLC_EXIT;
  } else if (ctrlCStatus == CTRLC_CHILD)
    exit(0);
}

void actionBeforeMainLoop() {
  mySigAction.sa_handler = &sigHandler;
  sigaction(SIGINT, &mySigAction, NULL);
  if (!(homeDir = getenv("HOME"))) {
    perror("");
    exit(0);
  }
  lastDir = malloc(MAXCHAR);
  memset(lastDir, 0, MAXCHAR);
  if (!getcwd(lastDir, MAXCHAR)) {
    perror("");
    free(lastDir);
    exit(0);
  }
  lastDirTmp = malloc(MAXCHAR);
  memset(lastDirTmp, 0, MAXCHAR);
  strcpy(lastDirTmp, lastDir);
  iFileName = malloc(MAXCHAR);
  memset(iFileName, 0, MAXCHAR);
  oFileName = malloc(MAXCHAR);
  memset(oFileName, 0, MAXCHAR);
  linesBg = malloc(sizeof(char *) * MAXCHAR);
  for (size_t i = 0; i < MAXCHAR; ++i)
    linesBg[i] = NULL;
  pidBgArr = malloc(sizeof(pid_t) * MAXCHAR);
}

int main() {
  actionBeforeMainLoop();
  // ==========
  // main loop
  // ==========
  while (1) {
    printf("myshell $ ");
    fflush(stdout); // use fflush() right after stdout that has no '\n'
    ctrlCStatus = CTRLC_PARENT;
    // ==========
    // receive complete input
    // ==========
    inputParamInitialize();
    char *lineWhole = malloc(MAXCHAR);
    memset(lineWhole, 0, MAXCHAR);
    while (1) {
      char lineInit[MAXCHAR];
      memset(lineInit, 0, MAXCHAR);
      // fgets() includes '\n', size ensures the ending '\0', here stdin ensures
      // a final '\n'
      if (!fgets(lineInit, MAXCHAR, stdin)) // gets EOF from ctrl+c or ctrl+d
      {
        // EOF from ctrl+c
        if (ctrlCStatus == CTRLC_EXIT)
          break;
        // EOF from ctrl+d
        printf("exit\n");
        freeOuter();
        free(lineWhole);
        exit(0);
      }
      // ==========
      // check incomplete quotes and syntax error
      // ==========
      size_t lenLineInit = strlen(lineInit); // strlen() excludes the endng '\0'
      --lenLineInit;                         // excludes the final '\n'
      for (size_t i = 0; i < lenLineInit; ++i) {
        if (lineInit[i] == '\'') {
          if (isSQClosed && isDQClosed &&
              (i == 0 || (i > 0 && lineInit[i - 1] != '\\')))
            isSQClosed = 0;
          else if (!isSQClosed)
            isSQClosed = 1;
        } else if (lineInit[i] == '\"') {
          if (isDQClosed && isSQClosed &&
              (i == 0 || (i > 0 && lineInit[i - 1] != '\\')))
            isDQClosed = 0;
          else if (!isDQClosed &&
                   (i == 0 || (i > 0 && lineInit[i - 1] != '\\')))
            isDQClosed = 1;
        } else if (isFirstFgets) // check syntax error, here ensures only
                                 // occurring in the first fgets
        {
          if ((lineInit[i] == '<' || lineInit[i] == '>') && isSQClosed &&
              isDQClosed) {
            if (lineInit[i] == '>' && lineInit[i + 1] == '>')
              ++i;
            for (++i; i < lenLineInit; ++i) {
              if (lineInit[i] == ' ')
                continue;
              else if (lineInit[i] == '<' || lineInit[i] == '>' ||
                       lineInit[i] == '|') {
                hasIOError = 1;
                printf("syntax error near unexpected token `%c\'\n",
                       lineInit[i]);
                break;
              } else {
                --i;
                break;
              }
            }
          }
          if (hasIOError)
            break;
        }
      }
      // has syntax error
      if (isFirstFgets && hasIOError)
        break;
      isFirstFgets = 0;
      // incomplete quotes
      if (!isDQClosed || !isSQClosed) {
        printf("> ");
        fflush(stdout);
        strcat(lineWhole, lineInit);
        continue;
      }
      // check incomplete redirection or pipe, here >> is not considered
      for (int i = (int)lenLineInit - 1; i >= 0; --i) {
        if (lineInit[i] == ' ')
          continue;
        else if (lineInit[i] == '<' || lineInit[i] == '>' ||
                 lineInit[i] == '|') {
          isRPEnd = 0;
          break;
        } else {
          isRPEnd = 1;
          break;
        }
      }
      // incomplete redirecton or pipe
      if (!isRPEnd) {
        printf("> ");
        fflush(stdout);
        lineInit[lenLineInit] =
            ' '; // if incomplete rp, we don't need the final '\n'
        strcat(lineWhole, lineInit);
        continue;
      }
      // complete input
      strcat(lineWhole, lineInit);
      break;
    }
    // ==========
    // original complete input received
    // meet ctrl+c || syntax error || real empty input
    // ==========
    if (ctrlCStatus == CTRLC_EXIT || (isFirstFgets && hasIOError) ||
        lineWhole[0] == '\n') {
      free(lineWhole);
      continue;
    }
    // ==========
    // check background
    // ==========
    size_t lenLineWhole = strlen(lineWhole);
    lineWhole[--lenLineWhole] = '\0'; // discard final '\n'
    for (int i = (int)lenLineWhole - 1; i >= 0; --i) {
      if (lineWhole[i] == '&') {
        hasBg = 1;
        linesBg[numBg] = malloc(MAXCHAR);
        memset(linesBg[numBg], 0, MAXCHAR);
        strcpy(linesBg[numBg], lineWhole);
        // delete '&' and make sure end with "\n\0"
        lineWhole[i] = '\0';
        lenLineWhole = strlen(lineWhole);
        break;
      } else if (lineWhole[lenLineWhole - 1] == ' ')
        continue;
      else
        break;
    }
    // ==========
    // deal quotes,
    // decide special chars in quotes and quotation marks to be ignored,
    // do not modify lineWhole, only store ignored chars and special chars
    // ==========
    isSQClosed = 1;
    isDQClosed = 1;
    // index array of quotation marks to be ignored
    size_t *ignoredCharIdx = malloc(sizeof(size_t) * MAXCHAR);
    memset(ignoredCharIdx, 0, MAXCHAR);
    // special characters in quotes
    char *specialCharInQ = malloc(MAXCHAR);
    memset(specialCharInQ, 0, MAXCHAR);
    size_t numIgnoredChar = 0, numSpecialCharInQ = 0;
    for (size_t i = 0; i < lenLineWhole; ++i) {
      if (!isSQClosed || !isDQClosed) {
        if (lineWhole[i] == ' ') {
          lineWhole[i] = 13;
          specialCharInQ[numSpecialCharInQ++] = ' ';
        }
        if (lineWhole[i] == '\n') {
          lineWhole[i] = 13;
          specialCharInQ[numSpecialCharInQ++] = '\n';
        } else if (lineWhole[i] == '<') {
          lineWhole[i] = 13;
          specialCharInQ[numSpecialCharInQ++] = '<';
        } else if (lineWhole[i] == '>') {
          lineWhole[i] = 13;
          specialCharInQ[numSpecialCharInQ++] = '>';
        } else if (lineWhole[i] == '|') {
          lineWhole[i] = 13;
          specialCharInQ[numSpecialCharInQ++] = '|';
        }
      }
      if (lineWhole[i] == '\'') {
        if (isSQClosed && isDQClosed) {
          if (i > 0 && lineWhole[i - 1] == '\\')
            ignoredCharIdx[numIgnoredChar++] = i - 1;
          else {
            isSQClosed = 0;
            ignoredCharIdx[numIgnoredChar++] = i;
          }
        } else if (!isSQClosed) {
          isSQClosed = 1;
          ignoredCharIdx[numIgnoredChar++] = i;
        }
      } else if (lineWhole[i] == '\"') {
        if (i > 0 && lineWhole[i - 1] == '\\')
          ignoredCharIdx[numIgnoredChar++] = i - 1;
        else {
          if (isDQClosed && isSQClosed) {
            isDQClosed = 0;
            ignoredCharIdx[numIgnoredChar++] = i;
          } else if (!isDQClosed) {
            isDQClosed = 1;
            ignoredCharIdx[numIgnoredChar++] = i;
          }
        }
      }
    }
    // ==========
    // delete ignored chars
    // ==========
    char *lineIgnored = malloc(MAXCHAR); // no final '\n'
    memset(lineIgnored, 0, MAXCHAR);
    if (numIgnoredChar > 0) {
      for (size_t i = 0, j = 0, k = 0; i < lenLineWhole; ++i) {
        if (j < numIgnoredChar && i == ignoredCharIdx[j])
          ++j;
        else if (j >= numIgnoredChar ||
                 (j < numIgnoredChar && i != ignoredCharIdx[j]))
          lineIgnored[k++] = lineWhole[i];
      }
    } else
      strcpy(lineIgnored, lineWhole);
    free(lineWhole);
    free(ignoredCharIdx);
    // ==========
    // add spaces to lineIgnored
    // for convenience in tokenization
    // ==========
    char *lineAddSpace = malloc(MAXCHAR); // no final '\n'
    memset(lineAddSpace, 0, MAXCHAR);
    size_t lenLineIgnored = strlen(lineIgnored);
    for (size_t i = 0, j = 0; i < lenLineIgnored; ++i) {
      if (lineIgnored[i] == '<' || lineIgnored[i] == '>' ||
          lineIgnored[i] == '|') {
        lineAddSpace[j++] = ' ';
        lineAddSpace[j++] = lineIgnored[i];
        if (lineIgnored[i] == '>' && lineIgnored[i + 1] == '>')
          lineAddSpace[j++] = lineIgnored[++i];
        lineAddSpace[j++] = ' ';
      } else
        lineAddSpace[j++] = lineIgnored[i];
    }
    free(lineIgnored);
    // ==========
    // tokenize, extract redirection, mark pipe location, deal input error
    // ==========
    execParamInitialize();
    argv = malloc(sizeof(char *) * MAXCHAR);
    for (size_t i = 0; i < MAXCHAR; ++i)
      argv[i] = NULL;
    char *token = strtok(lineAddSpace, " ");
    while (token) {
      if (token[0] == '<') {
        if (hasIRdrct || numPipe > 0) {
          hasIOError = 1;
          printf("error: duplicated input redirection\n");
          break;
        }
        hasIRdrct = 1;
        token = strtok(NULL, " ");
        strcpy(iFileName, token);
        token = strtok(NULL, " ");
      } else if (token[0] == '>') {
        if (hasORdrct) {
          hasIOError = 1;
          printf("error: duplicated output redirection\n");
          break;
        }
        if (strcmp(token, ">>") == 0)
          hasORdrct = 2;
        else
          hasORdrct = 1;
        token = strtok(NULL, " ");
        strcpy(oFileName, token);
        token = strtok(NULL, " ");
      } else if (token[0] == '|') {
        token = strtok(NULL, " ");
        if (argc == 0 || token[0] == '|') {
          hasIOError = 1;
          printf("error: missing program\n");
          break;
        }
        if (hasORdrct) {
          hasIOError = 1;
          printf("error: duplicated output redirection\n");
          break;
        }
        // meet pipe
        // leave corresponding token in argv as NULL
        ++numPipe;
        ++argc;
      } else {
        argv[argc] = malloc(MAXCHAR);
        memset(argv[argc], 0, MAXCHAR);
        strcpy(argv[argc], token);
        ++argc;
        token = strtok(NULL, " ");
      }
    }
    free(lineAddSpace);
    // ==========
    // has input error
    // ==========
    if (argc == 0 && (hasIRdrct || hasORdrct)) {
      hasIOError = 1;
      printf("error: missing program\n");
    }
    if (hasIOError) {
      freeArgv();
      free(specialCharInQ);
      // if finally meet input error, delete bg input stored before
      if (hasBg) {
        free(linesBg[numBg]);
        linesBg[numBg] = NULL;
      }
      continue;
    }
    // ==========
    // retrive special chars in quotes
    // ==========
    if (numSpecialCharInQ > 0) {
      for (size_t i = 0, j = 0; j < numSpecialCharInQ; ++i) {
        if (argv[i]) {
          size_t lenToken = strlen(argv[i]);
          for (size_t k = 0; k < lenToken; ++k) {
            if (argv[i][k] == 13)
              argv[i][k] = specialCharInQ[j++];
          }
        }
      }
    }
    free(specialCharInQ);
    // ==========
    // empty input (only spaces)
    // ==========
    if (!argv[0]) {
      freeArgv();
      continue;
    }
    // ==========
    // create pipe fd
    // ==========
    size_t numPipeFd = 2 * numPipe;
    int *pipeFd = malloc(sizeof(int) * numPipeFd);
    // pipeFd[0]: read end, pipeFd[1]: write end
    for (size_t i = 0; i < numPipeFd; i += 2) {
      if (pipe(pipeFd + i) == -1) {
        perror("");
        freeArgv();
        freeOuter();
        free(pipeFd);
        exit(0);
      }
    }
    // ==========
    // locate each cmd
    // ==========
    numCmd = numPipe + 1;
    size_t *cmdNameIdx = malloc(sizeof(size_t) * numCmd);
    cmdNameIdx[0] = 0;
    for (size_t i = 0, j = 1; i < argc; ++i) {
      if (!argv[i])
        cmdNameIdx[j++] = i + 1;
    }
    // ==========
    // execute
    // =========
    pid_t *pidArr = malloc(sizeof(pid_t) * numCmd);
    for (size_t iCmd = 0; iCmd < numCmd; ++iCmd) {
      size_t currCmdIdx = cmdNameIdx[iCmd];
      // ==========
      // built-in commands
      // ==========
      // exit
      if (strcmp(argv[currCmdIdx], "exit") == 0) {
        printf("exit\n");
        freeArgv();
        freeOuter();
        free(pipeFd);
        free(cmdNameIdx);
        free(pidArr);
        exit(0);
      }
      // cd
      else if (strcmp(argv[currCmdIdx], "cd") == 0) {
        if (!argv[currCmdIdx + 1] ||
            (strcmp(argv[currCmdIdx + 1], "~") == 0)) // cd || cd ~
        {
          if (chdir(homeDir) == -1) {
            perror("");
            continue;
          }
          strcpy(lastDir, lastDirTmp);
          strcpy(lastDirTmp, homeDir);
        } else if (strcmp(argv[currCmdIdx + 1], "-") == 0) // cd -
        {
          char cwdTmp[MAXCHAR];
          memset(cwdTmp, 0, MAXCHAR);
          if (!getcwd(cwdTmp, MAXCHAR) || chdir(lastDir) == -1) {
            perror("");
            continue;
          }
          printf("%s\n", lastDir);
          strcpy(lastDirTmp, lastDir);
          strcpy(lastDir, cwdTmp);
        } else // cd dirName
        {
          if (chdir(argv[currCmdIdx + 1]) == -1) {
            printf("%s: No such file or directory\n", argv[currCmdIdx + 1]);
            continue;
          }
          char cwdTmp[MAXCHAR];
          memset(cwdTmp, 0, MAXCHAR);
          if (!getcwd(cwdTmp, MAXCHAR)) {
            perror("");
            continue;
          }
          strcpy(lastDir, lastDirTmp);
          strcpy(lastDirTmp, cwdTmp);
        }
        continue;
      }
      // jobs
      else if (strcmp(argv[currCmdIdx], "jobs") == 0) {
        for (size_t i = 0; i < numBg; ++i) {
          // use WNOHANG to return the status of given pid
          if (waitpid(pidBgArr[i], NULL, WNOHANG) == 0)
            printf("[%ld] running %s\n", i + 1, linesBg[i]);
          else
            printf("[%ld] done %s\n", i + 1, linesBg[i]);
        }
        continue;
      }
      // ==========
      // system call
      // ==========
      pid_t pid = fork();
      pidArr[iCmd] = pid;
      // receive background command
      if (pid > 0 && hasBg && iCmd == 0) {
        pidBgArr[numBg] = pid;
        printf("[%ld] %s\n", numBg + 1, linesBg[numBg]);
        // ++numBg when the program is indeed processing the bg input
        ++numBg;
      }
      // create child process failed
      if (pid == -1) {
        perror("");
        freeArgv();
        freeOuter();
        free(pipeFd);
        free(cmdNameIdx);
        free(pidArr);
        exit(0);
      }
      // ==========
      // child process
      // ==========
      else if (pid == 0) {
        ctrlCStatus = CTRLC_CHILD;
        sigaction(SIGINT, &mySigAction, NULL);
        // ==========
        // input redirection
        // ==========
        if (iCmd != 0) {
          if (dup2(pipeFd[2 * iCmd - 2], 0) == -1) {
            perror("");
            freeArgv();
            freeOuter();
            free(pipeFd);
            free(cmdNameIdx);
            free(pidArr);
            exit(0);
          }
        } else if (hasIRdrct) {
          if ((iFd = open(iFileName, O_RDONLY)) == -1) {
            if (errno == ENOENT)
              printf("%s: No such file or directory\n", iFileName);
            freeArgv();
            freeOuter();
            free(pipeFd);
            free(cmdNameIdx);
            free(pidArr);
            exit(0);
          }
          if (dup2(iFd, 0) == -1) {
            perror("");
            freeArgv();
            freeOuter();
            free(pipeFd);
            free(cmdNameIdx);
            free(pidArr);
            exit(0);
          }
          close(iFd);
        }
        // ==========
        // output redirection
        // ==========
        if (iCmd != numCmd - 1) {
          if (dup2(pipeFd[2 * iCmd + 1], 1) == -1) {
            perror("");
            freeArgv();
            freeOuter();
            free(pipeFd);
            free(cmdNameIdx);
            free(pidArr);
            exit(0);
          }
        } else if (hasORdrct == 1) {
          if ((oFd = open(oFileName, O_CREAT | O_TRUNC | O_WRONLY, S_IRWXU)) ==
              -1) {
            if (errno == EPERM || errno == EROFS)
              printf("%s: Permission denied\n", oFileName);
            freeArgv();
            freeOuter();
            free(pipeFd);
            free(cmdNameIdx);
            free(pidArr);
            exit(0);
          }
          if (dup2(oFd, 1) == -1) {
            perror("");
            freeArgv();
            freeOuter();
            free(pipeFd);
            free(cmdNameIdx);
            free(pidArr);
            exit(0);
          }
          close(oFd);
        } else if (hasORdrct == 2) {
          if ((oFd = open(oFileName, O_CREAT | O_APPEND | O_WRONLY, S_IRWXU)) ==
              -1) {
            if (errno == EPERM || errno == EROFS)
              printf("%s: Permission denied\n", oFileName);
            perror("");
            freeArgv();
            freeOuter();
            free(pipeFd);
            free(cmdNameIdx);
            free(pidArr);
            exit(0);
          }
          if (dup2(oFd, 1) == -1) {
            perror("");
            freeArgv();
            freeOuter();
            free(pipeFd);
            free(cmdNameIdx);
            free(pidArr);
            exit(0);
          }
          close(oFd);
        }
        // redirection done, close fd
        for (size_t i = 0; i < numPipeFd; ++i)
          close(pipeFd[i]);
        // ==========
        // built in pwd
        // ==========
        if (strcmp(argv[currCmdIdx], "pwd") == 0) {
          char cwdTmp[MAXCHAR];
          if (!getcwd(cwdTmp, MAXCHAR))
            perror("");
          else
            printf("%s\n", cwdTmp);
          freeArgv();
          freeOuter();
          free(pipeFd);
          free(cmdNameIdx);
          free(pidArr);
          exit(0);
        }
        // ==========
        // system call
        // execvp returns only when error occurs, otherwise auto ends curr
        // process
        // ==========
        if (execvp(argv[currCmdIdx], argv + currCmdIdx) == -1) {
          printf("%s: command not found\n", argv[currCmdIdx]);
          freeArgv();
          freeOuter();
          free(pipeFd);
          free(cmdNameIdx);
          free(pidArr);
          exit(0);
        }
      }
    }
    // ==========
    // parent process
    // ==========
    // should be ahead of waitpid
    // only when ALL REFERENCES to the fd is closed, can the process ends
    // so to let child process end,
    // we must first close references from parent
    for (size_t i = 0; i < numPipeFd; ++i)
      close(pipeFd[i]);
    // no background, wait all child processes
    if (!hasBg) {
      for (size_t i = 0; i < numCmd; ++i)
        waitpid(pidArr[i], NULL, WUNTRACED);
    }
    // ready for next loop
    freeArgv();
    free(pipeFd);
    free(cmdNameIdx);
    free(pidArr);
  }
  return 0;
}
