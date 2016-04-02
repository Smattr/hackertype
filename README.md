# Hackertype

Partially inspired by [hackertyper.net](hackertyper.net), Hackertype is a program
for following a pre-written script as you type, regardless of your input. It
was conceived for use in terminal-based demonstrations where it is difficult to
remember a long sequence of instructions or you are worried about making
mistakes under pressure. If it's still not clear to you what Hackertype is, the
example below may help.

## Building

Simply run `make`. There are no significant dependencies, though the technique
used may only work on Linux.

## Example

The directory, example, contains a script for typing some C code into Vim. To
try this out:

    hackertype --command "vim new_file.c" --script example/script

Then start hitting random keys and watch as some invented C starts appearing in
Vim.

## Support

If you have any issues or questions, please file something on the Bitbucket
tracker or email me
[matthew.fernandez@gmail.com](mailto:matthew.fernandez@gmail.com).

## Legal Stuff

This code is in the public domain. Use it in any way you see fit.
