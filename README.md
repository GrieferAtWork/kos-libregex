
# Submodule for KOS: libregex

This repository is a sub-module for [KOS](https://github.com/GrieferAtWork/KOSmk4) and exists for the sole purpose to allow other projects to make use of the regex compiler used to implement KOS's libc's `regcomp(3)` and `regexec(3)`.

Both [KOSmk4](https://github.com/GrieferAtWork/KOSmk4) and [deemon](https://github.com/GrieferAtWork/deemon) make use of this library, meaning that bugs found-- and fixes made here will affect both of these projects.
