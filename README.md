# CleanQ for Linux

This is the public repository of the CleanQ Linux implementation


## License

See the LICENSE file.


## Authors

 * Roni Haecki
 * Reto Achermann
 * Daniel Schwyn


## Contributing

See the CONTRIBUTING file.


## Dependencies

`make` and `gcc`

## Compiling and Running

To build CleanQ including the tests and examples, type

```
make
```

this will create the `build` directory containing the compiled libraries and
executables:

 * `build/lib`  contains libcleanq.
 * `build/include`  this directory contains the public include files
 * `bin`  the examplea and test directories.


## Building your own CleanQ application

If you like to build your own CleanQ workload you want to add the following
flags to your compiler:

Using the dynamic Library
```
INC=-I./build/include
LIB=-L../../build/lib -lcleanq -lrt
```

Using the static Library
```
INC=-I./build/include
LIB=./build/lib/libcleanq.a -lrt
```
