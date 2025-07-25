HTMW vT.1 only contains the source code and is still not very organized. You need to build the Lua library first, then use the following command to compile the executable
```
gcc htmw.c map.c nxlist.c nxvector.c nxstack.c -o htmw liblua.a
```
