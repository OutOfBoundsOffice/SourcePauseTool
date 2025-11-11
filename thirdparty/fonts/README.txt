Fonts are used for and loaded by ImGui. To generate the header file, download
https://github.com/ocornut/imgui/blob/master/misc/fonts/binary_to_compressed_c.cpp.
Then run:

binary_to_compressed_c.exe -nostatic ***.ttf ***_font > ***.hpp
