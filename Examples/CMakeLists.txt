add_executable(LighthouseWindowSceneExample
    WindowScene/main.cpp
)
target_link_libraries(LighthouseWindowSceneExample PRIVATE
    Lighthouse::GLFWInputBackend
    Lighthouse::Engine
    Lighthouse::OpenGLBackend
    Lighthouse::VulkanBackend
)
