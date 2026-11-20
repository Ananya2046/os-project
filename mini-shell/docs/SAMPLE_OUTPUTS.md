# Mini Linux Shell (`mysh`) - Sample Interactive Sessions & Test Runs

> **Course**: Operating Systems Lab (24B11CS312), JIIT Noida  
> **Topic**: Annotated Transcripts of Interactive Sessions and Test Suite Executions  

---

## Session 1: Basic Commands, Environment & Semicolon Chaining

```console
$ ./mysh
mysh> echo "Welcome to Mini Linux Shell!"
Welcome to Mini Linux Shell!
mysh> pwd
/Users/ananyaaggarwal/Desktop/os lab project/mini-shell
mysh> export COURSE=24B11CS312
mysh> echo "Operating Systems Lab Course Code: $COURSE"
Operating Systems Lab Course Code: 24B11CS312
mysh> echo "Current Shell Process ID: $$"
Current Shell Process ID: 24810
mysh> cd /tmp ; pwd
/tmp
mysh> cd -
/Users/ananyaaggarwal/Desktop/os lab project/mini-shell
mysh> pwd
/Users/ananyaaggarwal/Desktop/os lab project/mini-shell
```

---

## Session 2: I/O Redirection and Multi-Stage Pipelines

```console
mysh> cat << EOF > fruits.txt
mysh: command not found: EOF
mysh> echo "apple" > fruits.txt
mysh> echo "banana" >> fruits.txt
mysh> echo "cherry" >> fruits.txt
mysh> echo "apricot" >> fruits.txt
mysh> echo "avocado" >> fruits.txt
mysh> cat fruits.txt
apple
banana
cherry
apricot
avocado
mysh> cat fruits.txt | grep "^a"
apple
apricot
avocado
mysh> cat < fruits.txt | grep "^a" | tr a-z A-Z | sort -r > sorted_fruits.txt
mysh> cat sorted_fruits.txt
AVOCADO
APRICOT
APPLE
mysh> echo "Last pipeline exit status: $?"
Last pipeline exit status: 0
```

---

## Session 3: Background Jobs, Status Notification, and Zero Zombies

```console
mysh> sleep 5 &
[1] 25012
mysh> jobs
[1]+  Running              sleep 5 &
mysh> sleep 6
[1]+  Done                 sleep 5 &
mysh> jobs
mysh> ps -ef | grep "[d]efunct"
mysh> echo "Zero zombie processes remain in process table!"
Zero zombie processes remain in process table!
```

---

## Session 4: Job Control, Ctrl+Z, `bg`, `fg`, and Exit Warnings

```console
mysh> sleep 20
^Z
[1]+  Stopped                 sleep 20
mysh> jobs
[1]+  Stopped              sleep 20
mysh> bg %1
[1]+ sleep 20 &
mysh> jobs
[1]+  Running              sleep 20
mysh> fg %1
sleep 20
^C
mysh> jobs
mysh> sh -c 'kill -STOP $$'

[1]+  Stopped                 sh -c 'kill -STOP $$'
mysh> exit
There are stopped jobs.
mysh> exit
$ 
```

---

## Session 5: Automated Test Suite Output (`make test`)

```console
$ make test
./tests/run_tests.sh

======================================================================
        MINI LINUX SHELL (mysh) - COMPREHENSIVE TEST SUITE           
======================================================================

Category 1: Simple Commands & Built-ins
  Test 1 : Simple echo execution matches bash                      [ PASS ]
  Test 2 : pwd built-in prints working directory                   [ PASS ]
  Test 3 : cd [dir] and cd - (OLDPWD navigation)                   [ PASS ]

Category 2: Invalid Commands & Syntax Errors
  Test 4 : Command not found reports error (exit 127)              [ PASS ]
  Test 5 : Syntax error: pipe at start of line                     [ PASS ]
  Test 6 : Syntax error: redirection without filename              [ PASS ]

Category 3: I/O Redirection
  Test 7 : Output redirection (>) truncates file                   [ PASS ]
  Test 8 : Output redirection (>>) appends to file                 [ PASS ]
  Test 9 : Combined input (<) and output (>) redirection           [ PASS ]

Category 4: Multi-Stage Pipelines
  Test 10: 2-stage pipeline: cmd1 | cmd2                           [ PASS ]
  Test 11: 3-stage pipeline: cat | grep | wc -l                    [ PASS ]
  Test 12: 4-stage pipeline with input/output redirection          [ PASS ]

Category 5: Background Jobs & Notification
  Test 13: Background execution (&) and Done notification          [ PASS ]

Category 6: Job Control (Stopped, fg, bg, exit warning)
  Test 14: Process stopping (SIGSTOP) and resume with bg           [ PASS ]
  Test 15: Exit warns on stopped jobs and terminates on repeat     [ PASS ]

Category 7: Signal Handling
  Test 16: Ctrl+C at shell prompt does not terminate shell         [ PASS ]
  Test 17: Ctrl+C terminates foreground job and returns to prompt  [ PASS ]

Category 8: Zombie Process Prevention
  Test 18: Reaping background processes: zero zombies (<defunct>)  [ PASS ]

Category 9: Quoting, Whitespace & Extensions
  Test 19: Single and double quotes preserve internal whitespace   [ PASS ]
  Test 20: Environment variable expansion ($VAR and $?)            [ PASS ]
  Test 21: Sequential execution via semicolon separator (;)        [ PASS ]
  Test 22: Pathname wildcard expansion (* and ? via glob)          [ PASS ]

======================================================================
TEST SUMMARY:
  Total Tests  : 22
  Passed Tests : 22
  Failed Tests : 0
  Success Rate : 100%
======================================================================

ALL TESTS PASSED SUCCESSFULLY!
```

