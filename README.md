HTMW vT.5 only contains the source code and it is not very organized yet. You need to build the Lua library first, then use the following command to compile the executable
```
gcc htmw.c map.c nxlist.c nxvector.c nxstack.c str.c syntaxer.c -o htmw liblua.a
```
