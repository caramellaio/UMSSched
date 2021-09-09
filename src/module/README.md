# Kernel module

This folder contains the ***UMS kernel module***.

## Compile and mount module

### Mount
```
> sudo sh mount.sh
```

### Unmount
```
> sudo sh unmount.sh
```

### Documentation

Documentation can be generated through doxygen. See: https://www.doxygen.nl/index.html

### Include in your user mode library

This is not recomended, a user mode implementation of this project already exists (see src/user). If you wish to re-implement the ioctl calls and the user mode mechanism include this header and have fun!
```
#include this_folder/ums_device.h
```
