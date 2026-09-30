# NI Side-Chain. cmake/NiPlugin.cmake builds the editor and the bundles. Its
# aux input bus needs nothing here -- iPlug2 reads the layout from config.h's
# PLUG_CHANNEL_IO -- but it is why the AU is an `aumf`.
ni_add_plugin(NISideChain
  SOURCES
    SideChain.cpp SideChain.h
    Params.cpp Params.h
    Wire.cpp Wire.h
  LINK sc_engine)
