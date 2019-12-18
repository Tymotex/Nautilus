// Name: Tim Zhang
// Date last modified: Sun 24 Nov 17:03:20 AEDT 2019

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <dirent.h>   
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <spawn.h>
#include <glob.h>
#include <stdbool.h>

#define MAX_LINE_CHARS 1024 
#define INTERACTIVE_PROMPT "cowrie> "
#define DEFAULT_PATH "/bin:/usr/bin"
#define WORD_SEPARATORS " \t\r\n"
#define DEFAULT_HISTORY_SHOWN 10

// These characters are always returned as single words
#define SPECIAL_CHARS "!><|"

// Redirection flags
#define NOT_REDIR 0 
#define REDIR_OUTPUT 1
#define REDIR_APPEND 2
#define REDIR_INPUT 4

// ===== Default functions ===== 
static void execute_command(char **words, char **path, char **environment);
static void do_exit(char **words);
static int is_executable(char *pathname);
static char **tokenize(char *s, char *separators, char *special_chars);
static void free_tokens(char **tokens);

// ===== My helper functions ===== 

// Returns true if the file at pathName exists
static bool fileExists(char *pathName); 

// Returns true if the file at pathName is a directory
static bool isDirectory(char *pathName);  

// Given an array of directory pathnames, search for a target file and return 
// the full pathname to target
static char *findInPath(char **paths, char *target);  

// Given an array of arguments, concatenates them all together with a white 
// space between each argument and return the final single string
static char *formString(char **words);  

// Given an array of arguments, counts and returns the number of non-NULL
// arguments
static int getWordCount(char **words);  

// Given an array of arguments, counts and returns the total number of 
// characters 
static int getCharCount(char **words);  

// Checks if the input string is numeric. Returns true if it is
static bool isNumber(char *argument);  

// Handles error message printing for programs that weren't executable
static void executionError(char **words, char *programPath);

// Given a program name and an array of paths to search, form the absolute path
// and return it
static char *getPathToProgram(char *program, char **path);

// ===== Subset 0 - Builtin cd and pwd ===== 

// Executes cd
static void cd(char **words);

// Executes pwd
static void pwd(void);

// ===== Subset 2 - History utilities ===== 

// Opens $HOME/.cowrie_history in a given a mode, eg. "r", "w", etc. and 
// returns the FILE *
static FILE *openHistory(char *mode);  

// Returns the number of lines in $HOME/.cowrie_history
static int getHistoryLineCount(void); 

// Prints the latest n entries in $HOME/.cowrie_history 
static void printLatestHistory(int n); 

// Appends the given command to $HOME/.cowrie_history
static void writeHistory(char **words);  

// Fetches the string corresponding to lineNumber in $HOME/.cowrie_history
static char *getCommandFromHistory(int lineNumber); 

// Called if ! or !n was typed. Retrieves the right command from history and 
// tokenizes it before returning it 
static char **getHistoryWords(char **words);  

// ===== Subset 3 - Globbing utilities ===== 

// Returns true if a wildcard symbol was found in line
static bool hasWildcard(char *line);  

// Given an array of strings, return the same array but with the string at 
// the target index deleted
static char **removeFromWords(char **words, int target);  

// Given an array of strings, a new string and a target index, return the 
// same array of strings but with the new string inserted at target
static char **injectIntoWords(char **words, char *newWord, int target);

// Given an array of strings, return the same array but with all wildcards 
// expanded out fully
static char **expandWildcards(char **words);  

// ===== Subset 4 - I/O redirection ===== 

// Separately handles redirection commands by cases
static void executeRedirection(char **words, char **path, char **environ);

// Returns true if the given argument is a builtin command
static bool isBuiltin(char *argument); 

// Given an array of arguments, returns true if a redirection symbol is present
static bool isRedirectionCommand(char **words);

// Returns true if the argument is a redirection symbol. This is useful for
// checking if the redirection command string is valid
static bool isRedirection(char *argument);

// Returns true if the given command is determined to be a valid redirection
// command
static bool checkRedirValidity(char **words, int flags);

// Returns an integer containing redirection flags REDIR_INPUT, REDIR_OUTPUT or
// REDIR_APPEND if the given arguments form a valid I/O redirection command
static int getRedirectionType(char **words);

// Given an array of strings and a separator, returns a slice of that array
// from argument 0 up to, but not including, the separator string
// eg. leftPartition({"hello", "<", "world", NULL}, "<") returns {"hello", NULL}
static char **leftPartition(char **words, char *separator);

// Given an array of strings and a separator, returns a slice of that array
// from argument 0 up to, but not including, the separator string
// eg. rightPartition({"hello", "<", "world", NULL}, "<") returns {"world", NULL}
static char **rightPartition(char **words, char *separator, bool stopLastOccurrence);

// Exclusively handles commands of the form: "< filename command ..."
static void executeRedirInput(char **words, char **path, char **environment);

// Exclusively handles commands of the form: "command ... > filename"
static void executeRedirOutput(char **words, char **path, char **environment);

// Exclusively handles commands of the form: "command ... >> filename"
static void executeRedirAppend(char **words, char **path, char **environment);

// Exclusively handles commands of the form: "< filename command ... > filename"
static void executeRedirInputAndOutput(char **words, char **path, char **environment);

// Exclusively handles commands of the form: "< filename command ... >> filename"
static void executeRedirInputAndAppend(char **words, char **path, char **environment);

// ===== Subset 5 - Piping between processes ===== 

// Returns true if the given command was identified as a piping command
static bool isPipeCommand(char **words);

// Returns the number of pipes found in the command's arguments
static int getNumberOfPipes(char **words);

// Executes the piping command 
static void executePiping(char **commandWords, char **path, char **environ);

// Recursively pipe across multiple processes, with each call passing across
// input arguments. 
static void handlePiping(char **commandWords, char **path, char **environ, 
                         char **inputArguments, int numberOfPipes, 
                         int redirectOption, char *outputFilename);

int main(void) {
    setlinebuf(stdout);
    extern char **environ;
    char *pathp;  
    if ((pathp = getenv("PATH")) == NULL) {
        pathp = DEFAULT_PATH;  
    }
    char **path = tokenize(pathp, ":", "");
    char *prompt = NULL;
    if (isatty(1)) {  
        prompt = INTERACTIVE_PROMPT;
    }
    while (1) { 
        if (prompt) {
            fputs(prompt, stdout);
        }
        char line[MAX_LINE_CHARS];
        if (fgets(line, MAX_LINE_CHARS, stdin) == NULL) {  
            break;
        }       
        char **commandWords = tokenize(line, WORD_SEPARATORS, SPECIAL_CHARS);
        // Make a copy the command arguments for history writing 
        char **commandWordsCopy = tokenize(line, WORD_SEPARATORS, SPECIAL_CHARS);
        
        if (commandWords != NULL && commandWords[0] != NULL) {
            if (isPipeCommand(commandWords)) {
                writeHistory(commandWordsCopy); 
                executePiping(commandWords, path, environ);
            } else if (isRedirectionCommand(commandWords)) {
                writeHistory(commandWordsCopy); 
                executeRedirection(commandWords, path, environ);
            } else if (strcmp(commandWords[0], "!") == 0) {
                char **historyWords = getHistoryWords(commandWords);
                if (historyWords != NULL) {
                    writeHistory(historyWords);
                    historyWords = expandWildcards(historyWords);
                    execute_command(historyWords, path, environ);
                    commandWords = expandWildcards(commandWords);
                    free(historyWords);
                }
            } else {
                commandWords = expandWildcards(commandWords);
                execute_command(commandWords, path, environ);
                writeHistory(commandWordsCopy);  
            }
        }
        free_tokens(commandWordsCopy);
        free_tokens(commandWords);
    }
    free_tokens(path);
    return 0;
}
    
static void execute_command(char **words, char **path, char **environment) {
    assert(words != NULL);
    assert(path != NULL);
    assert(environment != NULL);
    char *program = words[0];
    if (program == NULL) { return; }
    if (strcmp(program, "exit") == 0) {
        do_exit(words);
        return; 
    }
    int argc = getWordCount(words);
    // Subset 0:
    if (strcmp(program, "cd") == 0) {
        if (argc > 2) {
            fprintf(stderr, "%s: too many arguments\n", program);
        } else {
            cd(words);
        }
        return;
    }
    if (strcmp(program, "pwd") == 0) {
        if (argc > 1) {
            fprintf(stderr, "%s: too many arguments\n", program);
        } else {
            pwd();
        }
        return;
    }
    // Subset 2:
    if (strcmp(program, "history") == 0) { 
        if (argc == 1) {
            printLatestHistory(DEFAULT_HISTORY_SHOWN);
        } else if (argc == 2) {
            if (isNumber(words[1])) {
                printLatestHistory(atoi(words[1]));
            } else {
                fprintf(stderr, "%s: nonnumber: numeric argument required\n", program);
            }
        } else {
            fprintf(stderr, "%s: too many arguments\n", program);
        }
        return;
    }
    // Run a non built-in program
    char *programPath = getPathToProgram(words[0], path);
    if (programPath != NULL && is_executable(programPath)) {
        pid_t pid;
        if (posix_spawn(&pid, programPath, NULL, NULL, words, environment) != 0) {
            perror("spawn:");
            return;
        }
        int exit_status;
        if (waitpid(pid, &exit_status, 0) == -1) {
            perror("waitpid");
            return;
        }
        exit_status = WEXITSTATUS(exit_status); 
        printf("%s exit status = %d\n", programPath, exit_status);
    } else {
        executionError(words, programPath);
    }
}

// ================= Helper Functions =================

static bool fileExists(char *pathName) {
    struct stat s;
    if (stat(pathName, &s) == 0) {
        return true;
    } else {
        return false;
    }
}

static bool isDirectory(char *pathName) {
    if (fileExists(pathName)) {
        struct stat s;
        stat(pathName, &s);
        if (S_ISDIR(s.st_mode)) {
            return true;
        } else {
            return false;
        }
    }
    return false;    
}

static char *findInPath(char **paths, char *target) {
    char *absPathToTarget = NULL;
    char *path = NULL;
    for (int i = 0; paths[i] != NULL; i++) {
        path = paths[i];
        struct dirent *dir;
        // Open current directory
        DIR *curr = opendir(path);  
        if (curr != NULL) {
            while ((dir = readdir(curr)) != NULL) {
                if (strcmp(dir -> d_name, target) == 0) {
                    int size = sizeof(char) * (strlen(path) + strlen(target)) + 1 + 1;
                    absPathToTarget = malloc(size);
                    strcpy(absPathToTarget, path);
                    strcat(absPathToTarget, "/");
                    strcat(absPathToTarget, target);
                    return absPathToTarget;
                }
            }
        }
    }
    return absPathToTarget;
}

static char *formString(char **words) {
    assert(words != NULL && words[0] != NULL);
    int argc = getWordCount(words);
    int charCount = getCharCount(words);
    // String size calculation:
    // charCount is the number of characters, not including spaces, '\n' and '\0'
    // + (argc - 1) because that's how many spaces there are
    // + 1 for \n and + 1 for the null terminator \0
    char *result = malloc(sizeof(char) * (charCount + (argc - 1) + 1 + 1));  
    strcpy(result, words[0]);
    for (int i = 1; i < argc; i++) {
        strcat(result, " ");
        strcat(result, words[i]);
    }
    strcat(result, "\n");
    return result;
}

static int getWordCount(char **words) {
    int argc = 0;
    assert(words != NULL);
    while (words[argc] != NULL) {
        argc++;
    }
    return argc;
}

static int getCharCount(char **words) {
    int charCount = 0;
    int i = 0;
    while (words[i] != NULL) {
        charCount += strlen(words[i]);
        i++;
    }
    return charCount;
}

static bool isNumber(char *argument) {
    for (int i = 0; i < strlen(argument); i++) {
        char ch = argument[i];
        if (ch == '-' && i == 0 && strlen(argument) > 1) {
            // dash - indicates negative number when it's the first character of a string of more than 2 characters
            // so ignore this iteration 
            continue;  
        }
        if (!(ch >= '0' && ch <= '9')) {
            return false;
        }
    }
    return true;
}

static void executionError(char **words, char *programPath) {
    if (programPath != NULL && fileExists(programPath) == false) {
        fprintf(stderr, "%s: command not found\n", programPath);
    } else if (programPath == NULL) {
        fprintf(stderr, "%s: command not found\n", words[0]);            
    } else {
        fprintf(stderr, "%s: Permission denied\n", programPath);
    }
}

static char *getPathToProgram(char *program, char **path) {
    char *programPath = program;
    char *programName = NULL;
    if ((programName = strrchr(programPath, '/')) == NULL) {
        programPath = findInPath(path, program);  
    }
    return programPath;
}

// ===================== SUBSET 0 =====================
static void cd(char **words) {
    bool noDirectory = false;
    if (words[1] != NULL) {
        // chdir returns 0 on success
        if (chdir(words[1]) != 0) {
            noDirectory = true;
        }
    } else {
        char *homePath = getenv("HOME");
        if (chdir(homePath) != 0) {
            perror("chdir:");
        }
    }    
    if (noDirectory) {
        fprintf(stderr, "cd: %s: No such file or directory\n", words[1]);
    } 
}

static void pwd(void) {
    char *cwd = malloc(sizeof (char) * MAX_LINE_CHARS);
    getcwd(cwd, MAX_LINE_CHARS);
    printf("current directory is '%s'\n", cwd);
    free(cwd);
}

// ===================== SUBSET 2 =====================

static FILE *openHistory(char *mode) {
    char *home = getenv("HOME");
    char historyFilename[] = ".cowrie_history";
    // + 1 for '/' and + 1 for '\0'
    char *fullPath = malloc(sizeof(char) * (strlen(home) + strlen(historyFilename) + 1 + 1));  
    strcpy(fullPath, home);
    strcat(fullPath, "/");
    strcat(fullPath, historyFilename);
    FILE *historyFile = fopen(fullPath, mode);
    free(fullPath);
    return historyFile;
}

static int getHistoryLineCount() {
    FILE *file = openHistory("r");
    if (file == NULL) {
        return 0;
    }
    int lineCount = 0;
    char buffer[BUFSIZ];
    while(fgets(buffer, BUFSIZ, file) != NULL) {
        lineCount++;
    }
    fclose(file);
    return lineCount;
}

static void printLatestHistory(int n) {
    FILE *historyFile = openHistory("r");
    if (historyFile == NULL) {
        return;
    }
    // Print lines (lineCount - n) to lineCount
    int lineCount = getHistoryLineCount();
    if (n > lineCount) { 
        n = lineCount;
    }
    int currLine = 0;
    char line[MAX_LINE_CHARS];
    while (fgets(line, MAX_LINE_CHARS, historyFile) != NULL) {
        if (currLine >= (lineCount - n) && currLine < lineCount) {
            printf("%d: %s", currLine, line);
        }
        currLine++;
    }
    fclose(historyFile);
}

static void writeHistory(char **words) {
    char *inputText = formString(words);
    FILE *historyFile = openHistory("a");
    fputs(inputText, historyFile);
    free(inputText);    
    fclose(historyFile);
}

static char *getCommandFromHistory(int lineNumber) {
    FILE *historyFile = openHistory("r");
    if (historyFile == NULL) {
        return NULL;  
    }
    int currLine = 0;
    char line[BUFSIZ];  
    while (fgets(line, BUFSIZ, historyFile) != NULL) { 
        if (currLine == lineNumber) {
            break;
        }
        currLine++;
    }
    // + 1 slot for NULL terminator '\0'
    char *result = malloc(sizeof(char) * (strlen(line) + 1));  
    strcpy(result, line); 
    fclose(historyFile);
    return result;
}

static char **getHistoryWords(char **words) {
    int argc = getWordCount(words);
    char **historyWords = NULL;
    int lineCount = getHistoryLineCount();
    if (argc == 1) {
        if (lineCount > 0) { 
            // -1 because history's lines are indexed starting from 0
            char *command = getCommandFromHistory(lineCount - 1);  
            printf("%s", command);
            historyWords = tokenize(command, WORD_SEPARATORS, SPECIAL_CHARS);       
            free(command);
        } else {
            fprintf(stderr, "%s: invalid history reference\n", words[0]);
        }
    } else if (argc == 2) {
        if (isNumber(words[1])) {
            int lineNumber = atoi(words[1]);
            if (lineNumber > lineCount || lineNumber < 0) {
                fprintf(stderr, "%s: invalid history reference\n", words[0]);
            } else {
                char *command = getCommandFromHistory(lineNumber);
                printf("%s", command);
                historyWords = tokenize(command, WORD_SEPARATORS, SPECIAL_CHARS);
                free(command);
            }
        } else {
            fprintf(stderr, "%s: %s: numeric argument required\n", words[0], words[1]);
        }
    } else {
        fprintf(stderr, "%s: too many arguments\n", words[0]);
    }
    return historyWords;
}

// ===================== SUBSET 3 =====================

static bool hasWildcard(char *line) {
    for (int n = 0; n < strlen(line); n++) {
        if (line[n] == '*' ||
            line[n] == '?' ||
            line[n] == '[' ||
            line[n] == ']' ||
            line[n] == '~') {
            return true;
        }
    }
    return false;
}

static char **removeFromWords(char **words, int target) {
    int argc = getWordCount(words);
    int newSize = sizeof(char *) * argc;
    char **newWords = malloc(newSize);
    int n = 0;
    for (int i = 0; i < argc; i++, n++) {
        if (n == target) {
            // Incremement n to skip copying the word that was deleted
            n++;  
        } 
        newWords[i] = words[n];
    }
    assert(newWords[argc - 1] == NULL);
    free(words);
    return newWords;
}

static char **injectIntoWords(char **words, char *newWord, int target) {
    // Find size of words (number of pointers * 8)
    int argc = getWordCount(words);
    // + 1 because NULL should be the final entry
    int wordsSize = sizeof(char *) * (argc + 1);
    // Since we are adding just one additional entry to words  
    int newSize = wordsSize + sizeof(char *);  
    char **newWords = malloc(newSize);

    // Copy over contents from words to newWords 
    int n = 0;
    for (int i = 0; i < argc + 1; i++, n++) {
        if (i == target) {
            newWords[i] = newWord;
            n++;
        }
        newWords[n] = words[i];
    }
    newWords[argc + 1] = NULL;
    free(words);
    return newWords;
}

static char **expandWildcards(char **words) {
    for (int i = 0; i < getWordCount(words); i++) {
        if (hasWildcard(words[i])) {
            char *globArg = words[i];
            glob_t matches;
            int globResult = glob(globArg, GLOB_NOCHECK | GLOB_TILDE, NULL, &matches);    
            if (globResult == 0) {  
                // If globResult == 0, then glob has succeeded
                for (int n = 0; n < matches.gl_pathc; n++) {
                    if (n == 0) {
                        words = removeFromWords(words, i);  // Remove on first iteration only
                    }
                    words = injectIntoWords(words, matches.gl_pathv[n], i + n);  // Insert match into words
                }
            }
        }
    }
    return words;
}

// ===================== SUBSET 4 =====================

static void executeRedirection(char **words, char **path, char **environ) {
    // If the command attempts to redirect I/O, then handle the
    // command by the appropriate cases below:
    int flags = getRedirectionType(words);
    int redirectTypes = (REDIR_APPEND | REDIR_INPUT | REDIR_OUTPUT);
    words = expandWildcards(words);
    if ((flags & redirectTypes) ==  REDIR_INPUT) {
        executeRedirInput(words, path, environ);
    } else if ((flags & redirectTypes) ==  REDIR_OUTPUT)  {
        executeRedirOutput(words, path, environ);
    } else if ((flags & redirectTypes) == REDIR_APPEND) {
        executeRedirAppend(words, path, environ);
    } else if ((flags & redirectTypes) ==  (REDIR_INPUT | REDIR_OUTPUT)) {
        executeRedirInputAndOutput(words, path, environ);
    } else if ((flags & redirectTypes) ==  (REDIR_INPUT | REDIR_APPEND)) {
        executeRedirInputAndAppend(words, path, environ);
    }
}

static bool isBuiltin(char *argument) {
    if (strcmp(argument, "history") == 0 ||
        strcmp(argument, "cd") == 0 ||
        strcmp(argument, "pwd") == 0 ||
        strcmp(argument, "!") == 0) {
        fprintf(stderr, "%s: I/O redirection not permitted for builtin commands\n", argument);
        return true;
    } else {
        return false;
    }
}

static bool isRedirectionCommand(char **words) {
    for (int i = 0; i < getWordCount(words); i++) {
        if (strcmp(words[i], "<") == 0 ||
            strcmp(words[i], ">") == 0) {
            return true;
        }
    }
    return false;
}

static bool isRedirection(char *argument) {
    if (strcmp(argument, "<") == 0 ||
        strcmp(argument, ">") == 0) {
        return true;
    } else {
        return false;
    }
}

static bool checkRedirValidity(char **words, int flags) {
    int redirectTypes = (REDIR_APPEND | REDIR_INPUT | REDIR_OUTPUT);
    int argc = getWordCount(words);
    
    switch (flags & redirectTypes) {
        case 0:
            fprintf(stderr, "invalid input redirection\n");
            return false;
        case REDIR_INPUT:
            // If <, > or | is detected in the rest of the commands, then invalid redirection
            for (int i = 1; i < argc; i++) {
                if (isRedirection(words[i]) == true) {
                    fprintf(stderr, "invalid input redirection\n");
                    return false;
                }
            }
            break;
        case REDIR_OUTPUT:
            // Last argument must not be <, > or |
            if (isRedirection(words[argc - 1]) == true) {
                fprintf(stderr, "invalid input redirection\n");
                return false;
            }
            // If <, > or | is detected from words[0] to words[argc - 2], then invalid redirection
            for (int i = 0; i < argc - 2; i++) {
                if (isRedirection(words[i]) == true) {
                    fprintf(stderr, "invalid input redirection\n");
                    return false;
                }
            }
            break;
        case REDIR_APPEND:
            // Last argument must not be <, > or |
            if (isRedirection(words[argc - 1]) == true) {
                fprintf(stderr, "invalid input redirection\n");
                return false;
            }
            // If <, > or | is detected from words[0] to words[argc - 3], then invalid redirection
            for (int i = 1; i < argc - 3; i++) {
                if (isRedirection(words[i]) == true) {
                    fprintf(stderr, "invalid input redirection\n");
                    return false;
                }
            }
            break;
        case (REDIR_INPUT | REDIR_OUTPUT):
            // Commands must have a minimum of 5 arguments to be valid
            // Eg. shortest possible format: < file1 program > file2
            if (argc < 5) {
                fprintf(stderr, "invalid input redirection\n");
                return false;
            }
            // Last argument must not be <, > or |
            if (isRedirection(words[argc - 1]) == true) {
                fprintf(stderr, "invalid input redirection\n");
                return false;
            }
            // If <, > or | is detected from words[1] to words[argc - 2], then invalid redirection
            for (int i = 1; i < argc - 2; i++) {
                if (isRedirection(words[i]) == true) {
                    fprintf(stderr, "invalid input redirection\n");
                    return false;
                }
            }
            break;
        case (REDIR_INPUT | REDIR_APPEND):
            // Commands must have a minimum of 6 arguments to be valid
            // Eg. shortest possible format: < file1 program > > file2
            if (argc < 6) {
                fprintf(stderr, "invalid input redirection\n");
                return false;
            }
            // Last argument must not be <, > or |
            if (isRedirection(words[argc - 1]) == true) {
                fprintf(stderr, "invalid input redirection\n");
                return false;
            }
            // If <, > or | is detected from words[1] to words[argc - 3], then invalid redirection
            for (int i = 1; i < argc - 3; i++) {
                if (isRedirection(words[i]) == true) {
                    fprintf(stderr, "invalid input redirection\n");
                    return false;
                }
            }
            break;
    }
    return true;
}

static int getRedirectionType(char **words) {
    assert(words != NULL && words[0] != NULL);
    int argc = getWordCount(words);
    int flags = 0;

    if (argc <= 2) {
        return NOT_REDIR;
    }
    if (strcmp(words[0], "<") == 0) {
        flags = flags | REDIR_INPUT;
    }
    if (strcmp(words[argc - 3], ">") != 0 &&
        strcmp(words[argc - 2], ">") == 0) {
        flags = flags | REDIR_OUTPUT;
    }
    if (strcmp(words[argc - 3], ">") == 0 &&
        strcmp(words[argc - 2], ">") == 0) {
        flags = flags | REDIR_APPEND;
    }

    bool isValid = checkRedirValidity(words, flags);
    if (isValid == true) {
        return flags;
    } else {
        return 0;
    }
}

static char **leftPartition(char **words, char *separator) {
    char **leftWords = NULL;
    int argc = getWordCount(words);
    int separatorIndex = 0;
    bool separatorFound = false;
    for (int i = 0; i < argc; i++) {
        if (strcmp(words[i], separator) == 0) {
            // Reached the separator string
            separatorFound = true;
            break;
        }
        separatorIndex++;
    }
    if (separatorFound == false) {
        // Return the same input if this function failed to find the separator
        return words;
    }
    // (separatorIndex + 1) gives the size of the array slice (including NULL)
    leftWords = malloc(sizeof(char *) * (separatorIndex + 1));  
    // Copy contents of words
    for (int i = 0; i < separatorIndex; i++) {
        leftWords[i] = words[i];
    }
    // Set last element to be NULL
    leftWords[separatorIndex] = NULL;
    return leftWords;
}

static char **rightPartition(char **words, char *separator, bool stopLastOccurrence) {
    char **rightWords = NULL;
    int argc = getWordCount(words);
    int separatorIndex = 0;
    bool separatorFound = false;
    for (int i = 0; i < argc; i++) {
        if (strcmp(words[i], separator) == 0) {
            // Reached the separator string 
            separatorFound = true;
            separatorIndex = i;
            if (stopLastOccurrence == false) {
                break;
            }
        }
    }
    if (separatorFound == false) {
        // Return the same input if this function failed to find the separator
        return words;
    }
    // (argc - separatorIndex) gives the size of the array slice (including NULL)
    int size = (argc - separatorIndex);
    rightWords = malloc(sizeof (char *) * size); 
    // Copy contents of words
    for (int i = separatorIndex + 1, n = 0; i <= argc; i++, n++) {
        rightWords[n] = words[i];
    }
    // Set last element to be NULL
    rightWords[size - 1] = NULL;
    return rightWords;
}

static void executeRedirInput(char **words, char **path, char **environment) {
    assert(words != NULL && words[1] != NULL);
    // Second argument is always supposed to be a filename
    char *fileName = words[1]; 
    // All arguments after the filename are for executing a program
    char **rightWords = rightPartition(words, fileName, true);  
    int pipeFDs[2]; 
    pipe(pipeFDs);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, pipeFDs[0], 0);
    posix_spawn_file_actions_addclose(&actions, pipeFDs[1]);

    char *programName = rightWords[0];
    char *programPath = getPathToProgram(programName, path);
    if (isBuiltin(programName) == true) {
        return;
    }
    if (programPath != NULL && is_executable(programPath)) {
        FILE *inputFile = fopen(fileName, "r");
        if (inputFile == NULL) {
            fprintf(stderr, "%s: No such file or directory\n", fileName);
            return;  
        } 
        pid_t pid;
        posix_spawn(&pid, programPath, &actions, NULL, rightWords, environment);
        close(pipeFDs[0]);        
        FILE *programInput = fdopen(pipeFDs[1], "w");
        
        // Pipe in the lines from the input file into the program
        char line[256];
        while (fgets(line, 256, inputFile) != NULL) {
            fputs(line, programInput);
        }   
        fclose(inputFile);
        fclose(programInput);
        int exit_status;
        if (waitpid(pid, &exit_status, 0) == -1) {
            perror("waitpid");
            return;
        }
        exit_status = WEXITSTATUS(exit_status);
        printf("%s exit status = %d\n", programPath, exit_status);
    } else {
        executionError(rightWords, programPath);
    }
}

static void executeRedirOutput(char **words, char **path, char **environment) {
    int argc = getWordCount(words);
    // A filename is always expected as the last argument
    char *fileName = words[argc - 1];  
    char **leftWords = leftPartition(words, ">");
    int pipeFDs[2]; 
    pipe(pipeFDs);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addclose(&actions, pipeFDs[0]);
    posix_spawn_file_actions_adddup2(&actions, pipeFDs[1], 1);
    
    char *programName = leftWords[0];
    char *programPath = getPathToProgram(programName, path);
    if (isBuiltin(programName) == true) {
        return;
    }
    if (programPath != NULL && is_executable(programPath)) {
        if (isDirectory(fileName)) {
            fprintf(stderr, "%s: Is a directory\n", fileName);
            return;
        }
        pid_t pid;
        posix_spawn(&pid, programPath, &actions, NULL, leftWords, environment);
        close(pipeFDs[1]);
        
        // Pipe out lines from the program's output into the output file
        FILE *outputFile = fopen(fileName, "w");
        FILE *programOutput = fdopen(pipeFDs[0], "r");
        char line[MAX_LINE_CHARS];
        while (fgets(line, MAX_LINE_CHARS, programOutput) != NULL) {
            fputs(line, outputFile);
        }   
        fclose(outputFile);
        fclose(programOutput);
        int exit_status;
        if (waitpid(pid, &exit_status, 0) == -1) {
            perror("waitpid");
            return;
        }
        exit_status = WEXITSTATUS(exit_status); 
        printf("%s exit status = %d\n", programPath, exit_status);
    } else {
        executionError(leftWords, programPath);
    }
}

static void executeRedirAppend(char **words, char **path, char **environment) {
    int argc = getWordCount(words);
    // A filename is always expected as the last argument
    char *fileName = words[argc - 1]; 
    char **leftWords = leftPartition(words, ">");

    int pipeFDs[2]; 
    pipe(pipeFDs);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addclose(&actions, pipeFDs[0]);
    posix_spawn_file_actions_adddup2(&actions, pipeFDs[1], 1);
    
    char *programName = leftWords[0];
    char *programPath = getPathToProgram(programName, path);
    if (isBuiltin(programName) == true) {
        return;
    }
    if (programPath != NULL && is_executable(programPath)) {
        if (isDirectory(fileName)) {
            fprintf(stderr, "%s: Is a directory\n", fileName);
            return;
        }
        pid_t pid;
        posix_spawn(&pid, programPath, &actions, NULL, leftWords, environment);
        close(pipeFDs[1]);  
        
        // Pipe out lines from the program's output into the output file
        FILE *outputFile = fopen(fileName, "a");
        FILE *programOutput = fdopen(pipeFDs[0], "r");
        char line[MAX_LINE_CHARS];
        while (fgets(line, MAX_LINE_CHARS, programOutput) != NULL) {
            fputs(line, outputFile);
        }   
        fclose(outputFile);
        fclose(programOutput);
        int exit_status;
        if (waitpid(pid, &exit_status, 0) == -1) {
            perror("waitpid");
            return;
        }
        exit_status = WEXITSTATUS(exit_status);  
        printf("%s exit status = %d\n", programPath, exit_status);
    } else {
        executionError(leftWords, programPath);
    }
}

static void executeRedirInputAndOutput(char **words, char **path, char **environment) {
    assert(words != NULL && words[1] != NULL);
    int argc = getWordCount(words);
    char *inputFileName = words[1]; 
    char *outputFileName = words[argc - 1];
    char **rightWords = rightPartition(words, inputFileName, true); 
    // The actual command arguments are between '< inputfile' and '> outputfile'
    char **commandWords = leftPartition(rightWords, ">"); 
    
    int pipeWriteFD[2];
    int pipeReadFD[2]; 
    pipe(pipeWriteFD);
    pipe(pipeReadFD);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addclose(&actions, pipeWriteFD[1]);
    posix_spawn_file_actions_adddup2(&actions, pipeWriteFD[0], 0);
    posix_spawn_file_actions_addclose(&actions, pipeReadFD[0]);
    posix_spawn_file_actions_adddup2(&actions, pipeReadFD[1], 1);

    char *programName = commandWords[0];
    char *programPath = getPathToProgram(programName, path);
    if (isBuiltin(programName) == true) {
        return;
    }
    if (programPath != NULL && is_executable(programPath)) {
        FILE *inputFile = fopen(inputFileName, "r");
        if (inputFile == NULL) {
            fprintf(stderr, "%s: No such file or directory\n", inputFileName);
            return;
        }
        if (isDirectory(outputFileName)) {
            fprintf(stderr, "%s: Is a directory\n", outputFileName);
            return;
        }
        pid_t pid;
        posix_spawn(&pid, programPath, &actions, NULL, commandWords, environment);
        close(pipeWriteFD[0]);
        close(pipeReadFD[1]);
        
        FILE *programInput = fdopen(pipeWriteFD[1], "w");
        FILE *outputFile = fopen(outputFileName, "w");
        FILE *programOutput = fdopen(pipeReadFD[0], "r");

        // Pipe in the lines from the input file into the program
        char line[256];
        while (fgets(line, 256, inputFile) != NULL) {
            fputs(line, programInput);
        }   
        fclose(inputFile);
        fclose(programInput);
        
        // Pipe out lines from the program's output into the output file
        while (fgets(line, 256, programOutput) != NULL) {
            fputs(line, outputFile);
        }
        fclose(outputFile);
        fclose(programOutput);
        int exit_status;
        if (waitpid(pid, &exit_status, 0) == -1) {
            perror("waitpid");
            return;
        }
        exit_status = WEXITSTATUS(exit_status);
        printf("%s exit status = %d\n", programPath, exit_status);
    } else {
        executionError(commandWords, programPath);
    }
}

static void executeRedirInputAndAppend(char **words, char **path, char **environment) {
    assert(words != NULL && words[1] != NULL);
    int argc = getWordCount(words);
    char *inputFileName = words[1]; 
    char *outputFileName = words[argc - 1];
    char **rightWords = rightPartition(words, inputFileName, true);
    // The actual command arguments are between '< inputfile' and '>> outputfile' 
    char **commandWords = leftPartition(rightWords, ">"); 

    int pipeWriteFD[2];
    int pipeReadFD[2]; 
    pipe(pipeWriteFD);
    pipe(pipeReadFD);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addclose(&actions, pipeWriteFD[1]);
    posix_spawn_file_actions_adddup2(&actions, pipeWriteFD[0], 0);
    posix_spawn_file_actions_addclose(&actions, pipeReadFD[0]);
    posix_spawn_file_actions_adddup2(&actions, pipeReadFD[1], 1);

    char *programName = commandWords[0];
    char *programPath = getPathToProgram(programName, path);
    if (isBuiltin(programName) == true) {
        return;
    }
    if (programPath != NULL && is_executable(programPath)) {
        FILE *inputFile = fopen(inputFileName, "r");
        if (inputFile == NULL) {
            fprintf(stderr, "%s: No such file or directory\n", inputFileName);
            return;
        }
        if (isDirectory(outputFileName)) {
            fprintf(stderr, "%s: Is a directory\n", outputFileName);
            return;
        }
        pid_t pid;
        posix_spawn(&pid, programPath, &actions, NULL, commandWords, environment);
        close(pipeWriteFD[0]);
        close(pipeReadFD[1]);
        
        FILE *programInput = fdopen(pipeWriteFD[1], "w");
        FILE *outputFile = fopen(outputFileName, "a");
        FILE *programOutput = fdopen(pipeReadFD[0], "r");
        
        // Pipe in the lines from the input file into the program
        char line[256];
        while (fgets(line, 256, inputFile) != NULL) {
            fputs(line, programInput);
        }   
        fclose(inputFile);
        fclose(programInput);
        
        // Pipe out lines from the program's output into the output file
        while (fgets(line, 256, programOutput) != NULL) {
            fputs(line, outputFile);
        }
        fclose(outputFile);
        fclose(programOutput);
        int exit_status;
        if (waitpid(pid, &exit_status, 0) == -1) {
            perror("waitpid");
            return;
        }
        exit_status = WEXITSTATUS(exit_status);
        printf("%s exit status = %d\n", programPath, exit_status);
    } else {
        executionError(commandWords, programPath);
    }
}

// ===================== SUBSET 5 =====================

static bool isPipeCommand(char **words) {
    int argc = getWordCount(words);
    for (int i = 0; i < argc; i++) {
        if (strcmp(words[i], "|") == 0) {
            return true;
        }
    }
    return false;
}

static int getNumberOfPipes(char **words) {
    int count = 0;
    for (int i = 0; i < getWordCount(words); i++) {
        if (strcmp(words[i], "|") == 0) {
            count++;
        }
    }
    return count;
}

static void executePiping(char **commandWords, char **path, char **environ) {
    int argc = getWordCount(commandWords);
    int numberOfPipes = getNumberOfPipes(commandWords);
    int redirectOption = 0;
    commandWords = expandWildcards(commandWords);
    char **inputArguments = NULL;  
    char *outputFilename = NULL;
    if (isRedirectionCommand(commandWords) == true) {            
        int flags = getRedirectionType(commandWords);
        int redirTypes = REDIR_INPUT | REDIR_OUTPUT | REDIR_APPEND;
        if ((flags & redirTypes) == 0) {
            fprintf(stderr, "invalid input redirection\n");
            return;
        } 
        if ((flags & REDIR_OUTPUT) == REDIR_OUTPUT) {
            outputFilename = commandWords[argc - 1];
            commandWords = leftPartition(commandWords, ">");
            redirectOption = REDIR_OUTPUT;
        }
        if ((flags & REDIR_APPEND) == REDIR_APPEND) {
            outputFilename = commandWords[argc - 1];
            commandWords = leftPartition(commandWords, ">");
            redirectOption = REDIR_APPEND;
        }
        if ((flags & REDIR_INPUT) == REDIR_INPUT) {
            // If the command wants to pipe in lines from an input file, then
            // create an array of strings storing each of the lines
            inputArguments =  malloc(sizeof(char *) * 1);
            char *inputFilename = commandWords[1];
            FILE *inputFile = fopen(inputFilename, "r");
            if (inputFile == NULL) {
                fprintf(stderr, "%s: No such file or directory\n", inputFilename);
                return;
            }
            char inputLine[MAX_LINE_CHARS];
            int lineCount = 0;
            while(fgets(inputLine, MAX_LINE_CHARS, inputFile) != NULL) {
                char *newEntry = malloc(sizeof (char) * (strlen(inputLine) + 1));  
                strcpy(newEntry, inputLine);
                inputArguments = realloc(inputArguments, sizeof(char *) * (lineCount + 1));
                inputArguments[lineCount] = newEntry;
                lineCount++;
            }
            fclose(inputFile);
            inputArguments = realloc(inputArguments, sizeof(char *) * (lineCount + 1));
            inputArguments[lineCount] = NULL;
            commandWords = rightPartition(commandWords, commandWords[1], false);
        }
    }
    // Delegate to a recursive function to execute commands with one or more pipes
    handlePiping(commandWords, path, environ, inputArguments, numberOfPipes, redirectOption, outputFilename);
}

static void handlePiping(char **commandWords, char **path, char **environ, 
                         char **inputArguments, int numberOfPipes, 
                         int redirectOption, char *outputFilename) {
    // Each call to handlePiping deals with two processes and saves the output
    // for the next call
    char **rightChunk = rightPartition(commandWords, "|", false);
    char **leftProcess = leftPartition(commandWords, "|");
    char **rightProcess = leftPartition(rightChunk, "|");
    if (leftProcess[0] == NULL || rightProcess[0] == NULL) {
        printf("invalid pipe\n");
        return;
    }
    char **leftProcessOutput = malloc(sizeof(char *) * 1); 
    char **rightProcessOutput = malloc(sizeof(char *) * 1);
    int lineCount = 0;
    int outputLineCount = 0;
    int exit_status;

    int leftPipeInputFDs[2];
    int leftPipeOutputFDs[2];
    pipe(leftPipeInputFDs); 
    pipe(leftPipeOutputFDs);
    posix_spawn_file_actions_t leftActions;
    posix_spawn_file_actions_init(&leftActions);
    posix_spawn_file_actions_addclose(&leftActions, leftPipeOutputFDs[0]);
    posix_spawn_file_actions_adddup2(&leftActions, leftPipeOutputFDs[1], 1);
    posix_spawn_file_actions_addclose(&leftActions, leftPipeInputFDs[1]);
    posix_spawn_file_actions_adddup2(&leftActions, leftPipeInputFDs[0], 0);
    
    // Executing left process
    char *leftProgramName = leftProcess[0];
    char *leftProgramPath = getPathToProgram(leftProgramName, path);
    if (leftProgramPath != NULL && is_executable(leftProgramPath)) {
        pid_t pid;
        posix_spawn(&pid, leftProgramPath, &leftActions, NULL, leftProcess, environ);
        close(leftPipeOutputFDs[1]);
        close(leftPipeInputFDs[0]);

        FILE *programOutput = fdopen(leftPipeOutputFDs[0], "r");
        FILE *programInput = fdopen(leftPipeInputFDs[1], "w");
        // Pipe in the given input arguments into the left program's input    
        if (inputArguments != NULL) {
            int k = 0;
            while (inputArguments[k] != NULL) {
                fputs(inputArguments[k], programInput);
                k++;
            }
        }
        fclose(programInput);
        
        // Save the left program's input into leftProcessOutput
        char outputLine[MAX_LINE_CHARS];
        while (fgets(outputLine, MAX_LINE_CHARS, programOutput) != NULL) {
            char *newEntry = malloc(sizeof (char) * (strlen(outputLine) + 1));
            strcpy(newEntry, outputLine);
            leftProcessOutput = realloc(leftProcessOutput, sizeof(char *) * (lineCount + 1));
            leftProcessOutput[lineCount] = newEntry; 
            lineCount++;            
        }   
        // Set last element to be NULL
        leftProcessOutput = realloc(leftProcessOutput, sizeof(char *) * (lineCount + 1));
        leftProcessOutput[lineCount] = NULL;
        fclose(programOutput);
        if (waitpid(pid, &exit_status, 0) == -1) {
            perror("waitpid");
            return;
        }
    } else {
        executionError(leftProcess, leftProgramPath);
        return;
    }

    // Executing right process with left process' output
    int rightPipeInputFDs[2]; 
    int rightPipeOutputFDs[2]; 
    pipe(rightPipeInputFDs);
    pipe(rightPipeOutputFDs);
    posix_spawn_file_actions_t rightActions;
    posix_spawn_file_actions_init(&rightActions);
    posix_spawn_file_actions_adddup2(&rightActions, rightPipeInputFDs[0], 0);
    posix_spawn_file_actions_addclose(&rightActions, rightPipeInputFDs[1]);
    posix_spawn_file_actions_adddup2(&rightActions, rightPipeOutputFDs[1], 1);
    posix_spawn_file_actions_addclose(&rightActions, rightPipeOutputFDs[0]);

    char *rightProgramName = rightProcess[0];
    char *rightProgramPath = getPathToProgram(rightProgramName, path);
    if (rightProgramPath != NULL && is_executable(rightProgramPath)) {
        pid_t pid;
        posix_spawn(&pid, rightProgramPath, &rightActions, NULL, rightProcess, environ);
        close(rightPipeInputFDs[0]);
        close(rightPipeOutputFDs[1]);

        FILE *programInput = fdopen(rightPipeInputFDs[1], "w");
        FILE *programOutput = fdopen(rightPipeOutputFDs[0], "r");
        // Pipe in the output of the first process into the second process
        for (int i = 0; i < lineCount; i++) {
            char *inputLine = leftProcessOutput[i];
            fputs(inputLine, programInput);
        }
        fclose(programInput);

        // Save the right program's output into rightProcessOutput
        char outputLine[MAX_LINE_CHARS];
        while (fgets(outputLine, MAX_LINE_CHARS, programOutput) != NULL) {
            char *newEntry = malloc(sizeof (char) * (strlen(outputLine) + 1));
            strcpy(newEntry, outputLine);
            rightProcessOutput = realloc(rightProcessOutput, sizeof(char *) * (outputLineCount + 1));
            rightProcessOutput[outputLineCount] = newEntry; 
            outputLineCount++;
        }
        // Set last element to be NULL
        rightProcessOutput = realloc(rightProcessOutput, sizeof(char *) * (outputLineCount + 1));
        rightProcessOutput[outputLineCount] = NULL;  
        fclose(programOutput);
        if (waitpid(pid, &exit_status, 0) == -1) {
            perror("waitpid");
            return;
        }
        exit_status = WEXITSTATUS(exit_status);
    } else {
        executionError(rightProcess, rightProgramPath);
        return;
    }
    
    if (numberOfPipes == 1) {
        // This is the terminating case
        if (redirectOption == 0) {
            // No output redirection specified, print the output
            for (int i = 0; i < outputLineCount; i++) {
                printf("%s", rightProcessOutput[i]);
            }
        } else if ((redirectOption & REDIR_OUTPUT) == REDIR_OUTPUT) {
            // Redirect output into a file in write mode
            FILE *finalOutputFile = fopen(outputFilename, "w");
            for (int i = 0; i < outputLineCount; i++) {
                fputs(rightProcessOutput[i], finalOutputFile);
            }
            fclose(finalOutputFile);
        } else if ((redirectOption & REDIR_APPEND) == REDIR_APPEND) {
            // Redirect output into a file in append mode
            FILE *finalOutputFile = fopen(outputFilename, "a");
            for (int i = 0; i < outputLineCount; i++) {
                fputs(rightProcessOutput[i], finalOutputFile);
            }
            fclose(finalOutputFile);
        }
        printf("%s exit status = %d\n", rightProgramPath, exit_status);    
    } else {
        handlePiping(rightChunk, path, environ, rightProcessOutput, numberOfPipes - 1, redirectOption, outputFilename);
    }
}

// =================================================================

static void do_exit(char **words) {
    int exit_status = 0;
    if (words[1] != NULL) {
        if (words[2] != NULL) {
            fprintf(stderr, "exit: too many arguments\n");
        } else {
            char *endptr;
            exit_status = (int)strtol(words[1], &endptr, 10);
            if (*endptr != '\0') {
                fprintf(stderr, "exit: %s: numeric argument required\n",
                        words[1]);
            }
        }
    }
    exit(exit_status);
}

// Check whether this process can execute a file.
// Use this function when searching through the directories
// in the path for an executable file
static int is_executable(char *pathname) {
    struct stat s;
    return
        stat(pathname, &s) == 0 &&  
        S_ISREG(s.st_mode) &&      
        faccessat(AT_FDCWD, pathname, X_OK, AT_EACCESS) == 0; 
}

// Split a string 's' into pieces by any one of a set of separators.
//
// Returns an array of strings, with the last element being `NULL';
// The array itself, and the strings, are allocated with `malloc(3)';
// the provided `free_token' function can deallocate this.
static char **tokenize(char *s, char *separators, char *special_chars) {
    size_t n_tokens = 0;
    // malloc array guaranteed to be big enough
    char **tokens = malloc((strlen(s) + 1) * sizeof *tokens);

    while (*s != '\0') {
        // We are pointing at zero or more of any of the separators.
        // Skip leading instances of the separators.
        s += strspn(s, separators);

        // Now, `s' points at one or more characters we want to keep.
        // The number of non-separator characters is the token length.
        //
        // Trailing separators after the last token mean that, at this
        // point, we are looking at the end of the string, so:
        if (*s == '\0') {
            break;
        }

        size_t token_length = strcspn(s, separators);
        size_t token_length_without_special_chars = strcspn(s, special_chars);
        if (token_length_without_special_chars == 0) {
            token_length_without_special_chars = 1;
        }
        if (token_length_without_special_chars < token_length) {
            token_length = token_length_without_special_chars;
        }
        char *token = strndup(s, token_length);
        assert(token != NULL);
        s += token_length;

        // Add this token.
        tokens[n_tokens] = token;
        n_tokens++;
    }

    tokens[n_tokens] = NULL;
    // shrink array to correct size
    tokens = realloc(tokens, (n_tokens + 1) * sizeof *tokens);

    return tokens;
}

// Free an array of strings as returned by `tokenize'.
static void free_tokens(char **tokens) {
    for (int i = 0; tokens[i] != NULL; i++) {
        free(tokens[i]);
    }
    free(tokens);
}
