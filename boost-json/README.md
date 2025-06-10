# Boost.JSON

## Requirements
* Requires only C++11
* Link to a built static or dynamic Boost library, or use header-only
* Additional link to Boost.Container may be required (as described in its documentation)
* Supports -fno-exceptions, detected automatically

### Embedded
Boost.JSON works great on embedded devices. The library uses local stack buffers to increase the performance of some operations. On Intel platforms these buffers are large (4KB), while on non-Intel platforms they are small (256 bytes). To adjust the size of the stack buffers for embedded applications define this macro when building the library or including the function definitions:
```cpp
#define BOOST_JSON_STACK_BUFFER_SIZE 1024
#include <boost/json/src.hpp>
```
