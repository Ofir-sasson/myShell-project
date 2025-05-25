# myShell - project
# Custom C Shell – Command Execution with Resource Limits, Matrix Operations, and Safety Enforcement

## Overview

This project is a custom shell implementation written in C for Unix-like systems.  
It provides advanced features beyond basic shell execution, including:

- Dangerous command filtering
- Resource limit enforcement (`cpu`, `mem`, `fsize`, `nofile`)
- Pipe (`|`) and background (`&`) execution
- Logging of execution time and stats
- Custom `my_tee` command
- Matrix calculations with multithreaded tree reduction

---

## Features

### 🔒 Dangerous Command Filtering
- Loads a list of blocked commands from a user-provided file.
- **Exact matches** are blocked with an error.
- **Partial matches** trigger warnings.

### 🧠 Command Execution Modes
- Supports **foreground and background** execution.
- Handles complex commands with:
  - Pipes (`cmd1 | cmd2`)
  - Error redirection (`2>`)
  - Background jobs (`&` at end of line)

### ⏱️ Execution Statistics
Each command prompt shows:
- Total number of legal commands executed
- Blocked dangerous commands
- Last, average, min, and max execution time

Example prompt:
`#cmd:2|#dangerous_cmd_blocked:1|last_cmd_time:0.01420|avg_time:0.01003|min_time:0.00530|max_time:0.01420>>`


### 📈 Logging
- Logs successful command execution times to a specified file.

---

## 📊 Matrix Calculation (`mcalc`)
Multithreaded matrix addition/subtraction:
### Format:
`mcalc "(rows,cols:val1,val2,...)" "(rows,cols:val1,val2,...)" ADD|SUB`

Features:
Validates matrix dimensions
Uses a binary tree reduction approach with pthreads
Returns a single reduced matrix and prints it:
`# Output: (2,2:6,8,10,12)`

---

Custom my_tee Command
Redirects standard input to multiple files and stdout.

Usage:
echo hello | my_tee file1.txt file2.txt
echo world | my_tee -a file1.txt  # Append mode


---

Resource Limits (rlimit)
Commands can be restricted using:

cpu: max CPU seconds
mem: virtual memory size (B, KB, MB, GB)
fsize: max size for created files
nofile: number of open file descriptors

Example:
`rlimit set cpu=1 mem=5M fsize=1M nofile=10 ./some_binary`
Show Limits:
`rlimit show`

Signal Handling
Gracefully catches and reports:
-SIGXCPU: CPU time exceeded → CPU time limit exceeded!
-SIGXFSZ: file too large → File size limit exceeded!
-SIGSEGV: bad memory → Memory allocation failed!
-SIGUSR1: open file limit → Too many open files!
-SIGCHLD: cleans up background jobs

# Compilation (for the latest vertion)
gcc ex3.c -o ex3 -Wall

# Run 
./ex3 <dangerous_commands_file> <log_file>
Example:
./ex3 dangerous.txt log.txt

# Input 
Input is given interactively via the terminal.
Type done to exit.

# Output 
Output shown on screen:
* Dynamic command prompt
* Warnings/errors for dangerous commands
Output written to log file:
* Command and time taken if successful
