# NI Trance Gate. cmake/NiPlugin.cmake builds the editor and the bundles.
ni_add_plugin(NITranceGate
  SOURCES
    TranceGate.cpp TranceGate.h
    Params.cpp Params.h
    Patch.cpp Patch.h
  LINK tg_engine)
