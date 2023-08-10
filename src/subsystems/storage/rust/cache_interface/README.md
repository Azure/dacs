## Setting up
The rust build isn't wired up with the rest of the build yet.
Steps to run (all steps are expected to be run from inside the build container):
1. install rust (one time per build container): 
```
curl --proto '=https' --tlsv1.2 https://sh.rustup.rs -sSf | sh
# source ~/.bashrc if unable to run rustc/cargo etc
``` 
2. Finish building the dacs project, we are interested in the output of the cache_client project
3. Copy over the built libcache_client.so to ```/usr/lib/x86_64-linux-gnu/```, set appropriate permissions as well
```
chmod 0755 /usr/lib/x86_64-linux-gnu/libcache_client.so
```
4. Copy over the interface header file to ```/usr/include/```
5. Build the rust project - ```cargo build``` or alternatively build and run with ```cargo run```