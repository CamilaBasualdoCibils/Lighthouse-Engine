include(FetchContent)

set(FG_BUILD_TEST OFF CACHE BOOL "Disable FrameGraph tests")
FetchContent_Declare(
    FrameGraph
    GIT_REPOSITORY https://github.com/skaarj1989/FrameGraph.git
    GIT_TAG f84d671aa8a8f2668768b1e9768907b0f95cb978
)
FetchContent_MakeAvailable(FrameGraph) 
