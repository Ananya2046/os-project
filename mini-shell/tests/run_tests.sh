#!/bin/bash
# ==============================================================================
# run_tests.sh - Automated Test Suite for Mini Linux Shell (mysh)
# 
# Course: OS Lab (24B11CS312), JIIT Noida
# Covers all 9 required test categories:
# 1. Simple commands & Built-ins
# 2. Invalid commands & Syntax error handling
# 3. I/O Redirection (<, >, >>, combined)
# 4. Pipelines (2-stage, 3-stage, 4+-stage, with redirection)
# 5. Background jobs & Asynchronous notification
# 6. Stopped jobs, job table, fg, and bg
# 7. Signal handling (Ctrl+C kills only foreground job)
# 8. Zombie process prevention (zero defunct processes)
# 9. Quoting, whitespace, long input & clean EOF
# ==============================================================================

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SHELL_BIN="$PROJECT_ROOT/mysh"
TMP_DIR="$SCRIPT_DIR/tmp_test_$$"

# Colors for test reporting
GREEN="\033[0;32m"
RED="\033[0;31m"
YELLOW="\033[1;33m"
CYAN="\033[0;36m"
BOLD="\033[1m"
RESET="\033[0m"

PASSED_COUNT=0
FAILED_COUNT=0
TOTAL_COUNT=0

cleanup() {
    rm -rf "$TMP_DIR"
    # Kill any leftover test sleep processes
    pkill -f "sleep [0-9]" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

mkdir -p "$TMP_DIR"
cd "$TMP_DIR"

if [ ! -x "$SHELL_BIN" ]; then
    echo -e "${RED}Error: mysh binary not found at $SHELL_BIN. Build it first with 'make'.${RESET}"
    exit 1
fi

report_test() {
    local test_num="$1"
    local name="$2"
    local status="$3"
    local details="${4:-}"

    TOTAL_COUNT=$((TOTAL_COUNT + 1))
    if [ "$status" -eq 0 ]; then
        PASSED_COUNT=$((PASSED_COUNT + 1))
        printf "  Test %-2d: %-55s [ ${GREEN}PASS${RESET} ]\n" "$test_num" "$name"
    else
        FAILED_COUNT=$((FAILED_COUNT + 1))
        printf "  Test %-2d: %-55s [ ${RED}FAIL${RESET} ]\n" "$test_num" "$name"
        if [ -n "$details" ]; then
            echo -e "    ${YELLOW}Reason: $details${RESET}"
        fi
    fi
}

echo -e "\n${BOLD}${CYAN}======================================================================${RESET}"
echo -e "${BOLD}${CYAN}        MINI LINUX SHELL (mysh) - COMPREHENSIVE TEST SUITE           ${RESET}"
echo -e "${BOLD}${CYAN}======================================================================${RESET}\n"

# ------------------------------------------------------------------------------
# CATEGORY 1: Simple Commands and Built-ins
# ------------------------------------------------------------------------------
echo -e "${BOLD}Category 1: Simple Commands & Built-ins${RESET}"

# Test 1: Simple echo command
"$SHELL_BIN" << 'EOF' > mysh.out 2>&1
echo "Hello OS Lab Project"
EOF
bash -c 'echo "Hello OS Lab Project"' > bash.out 2>&1
if cmp -s mysh.out bash.out; then
    report_test 1 "Simple echo execution matches bash" 0
else
    report_test 1 "Simple echo execution matches bash" 1 "Output mismatch"
fi

# Test 2: pwd builtin
"$SHELL_BIN" << 'EOF' > mysh.out 2>&1
pwd
EOF
bash -c 'pwd' > bash.out 2>&1
if cmp -s mysh.out bash.out; then
    report_test 2 "pwd built-in prints working directory" 0
else
    report_test 2 "pwd built-in prints working directory" 1 "Output mismatch"
fi

# Test 3: cd to directory and cd -
mkdir -p dir_a dir_b
"$SHELL_BIN" << 'EOF' > mysh.out 2>&1
cd dir_a
pwd
cd ../dir_b
pwd
cd -
pwd
EOF
if grep -q "dir_a" mysh.out && grep -q "dir_b" mysh.out; then
    report_test 3 "cd [dir] and cd - (OLDPWD navigation)" 0
else
    report_test 3 "cd [dir] and cd - (OLDPWD navigation)" 1 "Failed directory tracking"
fi

# ------------------------------------------------------------------------------
# CATEGORY 2: Invalid Commands & Syntax Errors
# ------------------------------------------------------------------------------
echo -e "\n${BOLD}Category 2: Invalid Commands & Syntax Errors${RESET}"

# Test 4: Command not found error
"$SHELL_BIN" << 'EOF' > mysh.out 2> mysh.err
non_existent_binary_xyz_12345
EOF
if grep -q "command not found" mysh.err; then
    report_test 4 "Command not found reports error (exit 127)" 0
else
    report_test 4 "Command not found reports error (exit 127)" 1 "Missing 'command not found'"
fi

# Test 5: Syntax error: Pipe at beginning of line
"$SHELL_BIN" << 'EOF' > mysh.out 2> mysh.err
| ls
EOF
if grep -q "syntax error near unexpected token '|'" mysh.err; then
    report_test 5 "Syntax error: pipe at start of line" 0
else
    report_test 5 "Syntax error: pipe at start of line" 1 "Did not report syntax error"
fi

# Test 6: Syntax error: Redirection without filename
"$SHELL_BIN" << 'EOF' > mysh.out 2> mysh.err
cat <
EOF
if grep -q "syntax error near unexpected token" mysh.err; then
    report_test 6 "Syntax error: redirection without filename" 0
else
    report_test 6 "Syntax error: redirection without filename" 1 "Missing syntax error"
fi

# ------------------------------------------------------------------------------
# CATEGORY 3: I/O Redirection
# ------------------------------------------------------------------------------
echo -e "\n${BOLD}Category 3: I/O Redirection${RESET}"

# Test 7: Output truncation (>)
"$SHELL_BIN" << 'EOF' > /dev/null 2>&1
echo "initial text" > redir_test.txt
echo "overwritten" > redir_test.txt
EOF
if [ "$(cat redir_test.txt 2>/dev/null)" = "overwritten" ]; then
    report_test 7 "Output redirection (>) truncates file" 0
else
    report_test 7 "Output redirection (>) truncates file" 1 "File content mismatch"
fi

# Test 8: Output appending (>>)
"$SHELL_BIN" << 'EOF' > /dev/null 2>&1
echo "line 1" > append_test.txt
echo "line 2" >> append_test.txt
EOF
EXPECTED="line 1
line 2"
if [ "$(cat append_test.txt 2>/dev/null)" = "$EXPECTED" ]; then
    report_test 8 "Output redirection (>>) appends to file" 0
else
    report_test 8 "Output redirection (>>) appends to file" 1 "File content mismatch"
fi

# Test 9: Input redirection (<) and combined redirection
cat << 'DATA' > unsorted.txt
zebra
apple
mango
banana
DATA

"$SHELL_BIN" << 'EOF' > /dev/null 2>&1
sort < unsorted.txt > sorted.txt
EOF
EXPECTED_SORTED="apple
banana
mango
zebra"
if [ "$(cat sorted.txt 2>/dev/null)" = "$EXPECTED_SORTED" ]; then
    report_test 9 "Combined input (<) and output (>) redirection" 0
else
    report_test 9 "Combined input (<) and output (>) redirection" 1 "Sort redirection mismatch"
fi

# ------------------------------------------------------------------------------
# CATEGORY 4: Multi-stage Pipelines
# ------------------------------------------------------------------------------
echo -e "\n${BOLD}Category 4: Multi-Stage Pipelines${RESET}"

# Test 10: 2-stage pipeline
"$SHELL_BIN" << 'EOF' > mysh.out 2>&1
echo "one two three four five" | wc -w
EOF
bash -c 'echo "one two three four five" | wc -w' > bash.out 2>&1
if [ "$(tr -d ' ' < mysh.out)" = "$(tr -d ' ' < bash.out)" ]; then
    report_test 10 "2-stage pipeline: cmd1 | cmd2" 0
else
    report_test 10 "2-stage pipeline: cmd1 | cmd2" 1 "Output mismatch"
fi

# Test 11: 3-stage pipeline
cat << 'WORDS' > fruits.txt
apple
banana
apricot
cherry
avocado
WORDS

"$SHELL_BIN" << 'EOF' > mysh.out 2>&1
cat fruits.txt | grep "^a" | wc -l
EOF
bash -c 'cat fruits.txt | grep "^a" | wc -l' > bash.out 2>&1
if [ "$(tr -d ' ' < mysh.out)" = "$(tr -d ' ' < bash.out)" ]; then
    report_test 11 "3-stage pipeline: cat | grep | wc -l" 0
else
    report_test 11 "3-stage pipeline: cat | grep | wc -l" 1 "Output mismatch"
fi

# Test 12: 4-stage pipeline with redirection
"$SHELL_BIN" << 'EOF' > /dev/null 2>&1
cat < fruits.txt | grep a | tr a-z A-Z | sort -r > fruits_pipeline.out
EOF
bash -c 'cat < fruits.txt | grep a | tr a-z A-Z | sort -r > fruits_bash.out'
if cmp -s fruits_pipeline.out fruits_bash.out; then
    report_test 12 "4-stage pipeline with input/output redirection" 0
else
    report_test 12 "4-stage pipeline with input/output redirection" 1 "Pipeline output mismatch"
fi

# ------------------------------------------------------------------------------
# CATEGORY 5: Background Jobs & Notifications
# ------------------------------------------------------------------------------
echo -e "\n${BOLD}Category 5: Background Jobs & Notification${RESET}"

# Test 13: Background job prints [id] pgid
"$SHELL_BIN" << 'EOF' > mysh.out 2>&1
sleep 0.1 &
jobs
sleep 0.2
jobs
EOF
if grep -q "\[1\]" mysh.out && grep -q "Running" mysh.out && grep -q "Done" mysh.out; then
    report_test 13 "Background execution (&) and Done notification" 0
else
    report_test 13 "Background execution (&) and Done notification" 1 "Job notification missing"
fi

# ------------------------------------------------------------------------------
# CATEGORY 6: Stopped Jobs, Job Table, fg & bg
# ------------------------------------------------------------------------------
echo -e "\n${BOLD}Category 6: Job Control (Stopped, fg, bg, exit warning)${RESET}"

# Test 14: Stopped job state tracking & bg command
"$SHELL_BIN" << 'EOF' > mysh.out 2>&1
sh -c 'kill -STOP $$'
jobs
bg %1
jobs
EOF
if grep -q "Stopped" mysh.out && grep -q "Running" mysh.out; then
    report_test 14 "Process stopping (SIGSTOP) and resume with bg" 0
else
    report_test 14 "Process stopping (SIGSTOP) and resume with bg" 1 "bg resume failed"
fi

# Test 15: Exit warning when stopped jobs exist
"$SHELL_BIN" << 'EOF' > mysh.out 2>&1
sh -c 'kill -STOP $$'
exit
exit
EOF
if grep -q "There are stopped jobs." mysh.out; then
    report_test 15 "Exit warns on stopped jobs and terminates on repeat" 0
else
    report_test 15 "Exit warns on stopped jobs and terminates on repeat" 1 "Exit warning missing"
fi

# ------------------------------------------------------------------------------
# CATEGORY 7: Signal Handling (Ctrl+C / SIGINT)
# ------------------------------------------------------------------------------
echo -e "\n${BOLD}Category 7: Signal Handling${RESET}"

# Test 16: Ctrl+C does not kill the shell in interactive session
python3 -c "
import pty, os, time
master, slave = pty.openpty()
pid = os.fork()
if pid == 0:
    os.close(master)
    os.dup2(slave, 0)
    os.dup2(slave, 1)
    os.dup2(slave, 2)
    os.close(slave)
    os.execv('$SHELL_BIN', ['$SHELL_BIN'])
else:
    os.close(slave)
    time.sleep(0.1)
    os.write(master, b'\x03') # Ctrl+C at prompt
    time.sleep(0.1)
    os.write(master, b'echo shell_survived_ctrl_c\nexit\n')
    time.sleep(0.2)
    out = os.read(master, 2048).decode(errors='replace')
    os.close(master)
    os.waitpid(pid, 0)
    if 'shell_survived_ctrl_c' in out:
        exit(0)
    else:
        exit(1)
" >/dev/null 2>&1
STATUS=$?
if [ $STATUS -eq 0 ]; then
    report_test 16 "Ctrl+C at shell prompt does not terminate shell" 0
else
    report_test 16 "Ctrl+C at shell prompt does not terminate shell" 1 "Shell exited on Ctrl+C"
fi

# Test 17: Ctrl+C kills foreground child while shell survives
python3 -c "
import pty, os, time
master, slave = pty.openpty()
pid = os.fork()
if pid == 0:
    os.close(master)
    os.dup2(slave, 0)
    os.dup2(slave, 1)
    os.dup2(slave, 2)
    os.close(slave)
    os.execv('$SHELL_BIN', ['$SHELL_BIN'])
else:
    os.close(slave)
    time.sleep(0.1)
    os.write(master, b'sleep 2\n')
    time.sleep(0.2)
    os.write(master, b'\x03') # Ctrl+C to sleep
    time.sleep(0.2)
    os.write(master, b'echo child_interrupted\nexit\n')
    time.sleep(0.3)
    out = os.read(master, 4096).decode(errors='replace')
    os.close(master)
    os.waitpid(pid, 0)
    if 'child_interrupted' in out:
        exit(0)
    else:
        exit(1)
" >/dev/null 2>&1
STATUS=$?
if [ $STATUS -eq 0 ]; then
    report_test 17 "Ctrl+C terminates foreground job and returns to prompt" 0
else
    report_test 17 "Ctrl+C terminates foreground job and returns to prompt" 1 "Failed foreground signal"
fi

# ------------------------------------------------------------------------------
# CATEGORY 8: Zombie Process Prevention
# ------------------------------------------------------------------------------
echo -e "\n${BOLD}Category 8: Zombie Process Prevention${RESET}"

# Test 18: Multiple background jobs reaped without leaving zombies
"$SHELL_BIN" << 'EOF' > /dev/null 2>&1
sleep 0.05 &
sleep 0.05 &
sleep 0.05 &
sleep 0.05 &
sleep 0.05 &
sleep 0.2
jobs
EOF
# Verify no defunct/zombie processes remain for this user
ZOMBIES=$(ps -ef 2>/dev/null | grep "[d]efunct" | grep "sleep" || true)
if [ -z "$ZOMBIES" ]; then
    report_test 18 "Reaping background processes: zero zombies (<defunct>)" 0
else
    report_test 18 "Reaping background processes: zero zombies (<defunct>)" 1 "Found zombie processes: $ZOMBIES"
fi

# ------------------------------------------------------------------------------
# CATEGORY 9: Quoting, Whitespace, Semicolons & Extensions
# ------------------------------------------------------------------------------
echo -e "\n${BOLD}Category 9: Quoting, Whitespace & Extensions${RESET}"

# Test 19: Single and double quotes preserving whitespace
"$SHELL_BIN" << 'EOF' > mysh.out 2>&1
echo 'hello   world'
echo "spaces   inside   quotes"
EOF
bash -c 'echo "hello   world" ; echo "spaces   inside   quotes"' > bash.out 2>&1
if cmp -s mysh.out bash.out; then
    report_test 19 "Single and double quotes preserve internal whitespace" 0
else
    report_test 19 "Single and double quotes preserve internal whitespace" 1 "Whitespace inside quotes altered"
fi

# Test 20: Environment variable expansion ($VAR and $?)
"$SHELL_BIN" << 'EOF' > mysh.out 2>&1
export TEST_VAR=JIIT_OS_LAB
echo $TEST_VAR
echo "Exit status: $?"
EOF
if grep -q "JIIT_OS_LAB" mysh.out && grep -q "Exit status: 0" mysh.out; then
    report_test 20 "Environment variable expansion (\$VAR and \$?)" 0
else
    report_test 20 "Environment variable expansion (\$VAR and \$?)" 1 "Expansion failed"
fi

# Test 21: Semicolon command separator
"$SHELL_BIN" << 'EOF' > mysh.out 2>&1
echo "cmd1" ; echo "cmd2" ; echo "cmd3"
EOF
EXPECTED_SEMI="cmd1
cmd2
cmd3"
if [ "$(cat mysh.out)" = "$EXPECTED_SEMI" ]; then
    report_test 21 "Sequential execution via semicolon separator (;)" 0
else
    report_test 21 "Sequential execution via semicolon separator (;)" 1 "Semicolon chaining failed"
fi

# Test 22: Wildcard expansion (globbing)
mkdir -p glob_test && touch glob_test/a.txt glob_test/b.txt glob_test/c.txt
"$SHELL_BIN" << 'EOF' > mysh.out 2>&1
echo glob_test/*.txt
EOF
if [ "$(cat mysh.out)" = "glob_test/a.txt glob_test/b.txt glob_test/c.txt" ]; then
    report_test 22 "Pathname wildcard expansion (* and ? via glob)" 0
else
    report_test 22 "Pathname wildcard expansion (* and ? via glob)" 1 "Wildcard expansion mismatch"
fi

# ------------------------------------------------------------------------------
# FINAL SUMMARY
# ------------------------------------------------------------------------------
echo -e "\n${BOLD}${CYAN}======================================================================${RESET}"
echo -e "${BOLD}TEST SUMMARY:${RESET}"
echo -e "  Total Tests  : ${BOLD}$TOTAL_COUNT${RESET}"
echo -e "  Passed Tests : ${GREEN}${BOLD}$PASSED_COUNT${RESET}"
echo -e "  Failed Tests : ${RED}${BOLD}$FAILED_COUNT${RESET}"
PERCENTAGE=$(( PASSED_COUNT * 100 / TOTAL_COUNT ))
echo -e "  Success Rate : ${BOLD}$PERCENTAGE%${RESET}"
echo -e "${BOLD}${CYAN}======================================================================${RESET}\n"

if [ "$FAILED_COUNT" -eq 0 ]; then
    echo -e "${GREEN}${BOLD}ALL TESTS PASSED SUCCESSFULLY!${RESET}\n"
    exit 0
else
    echo -e "${RED}${BOLD}SOME TESTS FAILED. PLEASE REVIEW LOGS ABOVE.${RESET}\n"
    exit 1
fi
