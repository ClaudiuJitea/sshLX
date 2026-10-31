// Command-line tools, in the sshLX program itself: list connections, run a
// command, copy files, manage keys. They use the same connections, saved
// passwords and known hosts as the window, and need no display.
#pragma once

bool isCliCommand(const char *word);
int runCli(int argc, char **argv);
