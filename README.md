Requires ONNXRuntime v1.13.1

put onnx v1.13.1 in lib


To compile:
```
$ mkdir build
$ cd build
$ cmake -DCMAKE_BUILD_TYPE=Release ..
$ make
```

To run example test:
```
$ export LD_LIBRARY_PATH=`pwd`/../lib
$ ./main ../example.wav
```



TODO:
* Clean awful code
* Split model and session to allow many sessions for one model
* Read tokens.txt or similar instead of hardcoding tokens.h
* Do same with parameters, or get from ONNX
* fbank snip_edges and kaldifeat difference :(
* Could save hidden states
* Could batch many inputs into one call
* Figure out licensing
