# README #

The Remote Backup Application ensures seamless synchronization between a folder on a client machine and another on a server machine. This application offers real-time, bi-directional syncing capabilities, ensuring your data is consistent across both client and server locations.

### Compiling ###
#### Prerequisites ####
- C++ compiler. 
- Boost Libraries.
- CMake

### Launching the Client ###

```
./Client [DIRECTORY_PATH] [SERVER_IP_ADDRESS] [SERVER_PORT]
```

### Launching the Server ###
```
./Server [SYNC_FOLDER_PATH] [PORT]
```
