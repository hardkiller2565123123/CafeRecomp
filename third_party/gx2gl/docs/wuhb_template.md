# CMake Pattern

Use the stock `wut` helpers so the app builds a normal `.rpx` and an
optional `.wuhb` for content:

```cmake
add_executable(my_app source/main.cpp)
target_link_libraries(my_app PRIVATE gx2gl33)

wut_create_rpx(my_app)
wut_create_wuhb(my_app
    CONTENT "${CMAKE_SOURCE_DIR}/wuhb/content"
    NAME "My App"
    SHORTNAME "MyApp"
    AUTHOR "Your Name"
)
```

Runtime libraries are installed separately from WUHB content.

Should be simple, and even simpler if you use the rpl. (not implemented yet)
