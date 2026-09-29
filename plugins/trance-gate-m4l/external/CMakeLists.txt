# The Max externals behind the Max for Live device.
#
# ONE SUBDIRECTORY PER OBJECT, AND THE DIRECTORY NAME IS THE OBJECT NAME.
# That is max-sdk-base's convention and not a preference: max-pretarget.cmake
# derives the CMake project from the folder it is included from, mapping "~"
# to "_tilde" on the way. A file laid out any other way builds an external
# named after whatever directory it happened to sit in.
add_subdirectory("tg.gate~")
