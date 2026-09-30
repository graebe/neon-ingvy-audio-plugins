# NI Listen-In. cmake/NiPlugin.cmake builds the editor and the bundles. It has
# no DSP: the house transport, audio-bus, is its whole engine.
ni_add_plugin(NIListenIn
  SOURCES
    ListenIn.cpp ListenIn.h
    State.cpp State.h
    Wire.cpp Wire.h
  LINK audio_bus_engine)
