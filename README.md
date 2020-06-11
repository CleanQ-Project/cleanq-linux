# CleanQ for Linux

This is the public repository of the CleanQ Linux implementation


## License

See the LICENSE file.


## Authors

Roni Haecki
Reto Achermann
Daniel Schwyn


## Contributing

See the CONTRIBUTING file.


## Dependencies

libnuma-dev

## Compiling and Running

**Bulding DPDK**

cd dpdk-stable-18.11.1 

make config install T=x86_64-native-linuxapp-gcc 


**Build applications**

export RTE_SDK=< path to dpdk-stable-18.11.1>

cd benchmark_cleanq

make
