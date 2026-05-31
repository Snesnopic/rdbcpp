# rdbcpp

Minimal cli utility to pack/unpack tencent RDB resource databases.

## building
```bash
mkdir build && cd build
cmake ..
cmake --build .
```

## usage
```bash
# pack a directory of files into an rdb container
./rdbcpp --pack ./my_files output.rdb

# unpack an rdb container into a directory
./rdbcpp --unpack input.rdb ./extracted_files
```
