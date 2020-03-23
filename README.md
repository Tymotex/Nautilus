# Nautilus
A simple shell written in C.

In addition to executing binaries, Nautilus handles basic I/O redirection for files, 
piping I/O between processes and wildcard expansion. Commands are appended to a 
history file, ".nautilus_history" in the $HOME directory.

### Quick Setup:
1. Clone this repo with
```
git clone https://github.com/Tymotex/Nautilus.git
```
2. Compile and create a binary
```C
gcc -o nautilus nautilus.c
```
3. Run the shell
```
./nautilus
```
