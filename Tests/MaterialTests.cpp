#include <Lighthouse/Engine/Assets/Assets.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <string>

namespace {
using namespace Lighthouse;

std::filesystem::path MaterialFixture(const char* name) {
    return std::filesystem::path(LIGHTHOUSE_TEST_SOURCE_DIR) /
           "materials" / name;
}

std::filesystem::path InvalidFixture(const char* name) {
    return std::filesystem::path(LIGHTHOUSE_TEST_SOURCE_DIR) /
           "Tests/Fixtures/Materials" / name;
}

void ExpectValidStage(const Shader& shader, ShaderStage stage, std::uint32_t executionModel) {
    const auto words = shader.GetSPIRV();
    ASSERT_GE(words.size(), 6u);
    EXPECT_EQ(words.front(), 0x07230203u);
    EXPECT_EQ(shader.GetStage(), stage);
    EXPECT_EQ(shader.GetEntryPoint(), stage == ShaderStage::Vertex
        ? "LighthouseVertex" : "LighthouseFragment");

    bool foundEntryPoint = false;
    for (std::size_t word = 5; word < words.size();) {
        const auto instruction = words[word];
        const auto wordCount = instruction >> 16;
        const auto opcode = instruction & 0xffff;
        ASSERT_GT(wordCount, 0u);
        ASSERT_LE(word + wordCount, words.size());
        if (opcode == 15 && wordCount >= 4 && words[word + 1] == executionModel)
            foundEntryPoint = true;
        word += wordCount;
    }
    EXPECT_TRUE(foundEntryPoint);

#ifdef LIGHTHOUSE_SPIRV_VAL_EXECUTABLE
    const auto path = std::filesystem::temp_directory_path() /
        (std::string("lighthouse-") + std::string(shader.GetEntryPoint()) + ".spv");
    {
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(words.data()),
                     static_cast<std::streamsize>(words.size_bytes()));
        ASSERT_TRUE(output.good());
    }
    const auto command = std::string("\"") + LIGHTHOUSE_SPIRV_VAL_EXECUTABLE +
        "\" --target-env vulkan1.1 \"" + path.string() + "\"";
    EXPECT_EQ(std::system(command.c_str()), 0);
    std::filesystem::remove(path);
#endif
}

bool ContainsFloat(const Shader& shader, float value) {
    const auto expected = std::bit_cast<std::uint32_t>(value);
    const auto words = shader.GetSPIRV();
    return std::find(words.begin(), words.end(), expected) != words.end();
}

TEST(MaterialTests, LoadsLitAndUnlitMaterialTypesIntoVulkanSpirvShaders) {
    const auto lit = Assets::LoadMaterial(MaterialFixture("SolidColorLit.slang"), "SolidColorLit");
    const auto unlit = Assets::LoadMaterial(MaterialFixture("SolidColorUnlit.slang"), "SolidColorUnlit");

    ExpectValidStage(lit.vertexShader, ShaderStage::Vertex, 0);
    ExpectValidStage(lit.fragmentShader, ShaderStage::Fragment, 4);
    ExpectValidStage(unlit.vertexShader, ShaderStage::Vertex, 0);
    ExpectValidStage(unlit.fragmentShader, ShaderStage::Fragment, 4);

    EXPECT_TRUE(ContainsFloat(lit.vertexShader, 0.125f));
    EXPECT_TRUE(ContainsFloat(unlit.vertexShader, 0.25f));
    EXPECT_TRUE(ContainsFloat(lit.fragmentShader, 0.2f));
    EXPECT_TRUE(ContainsFloat(unlit.fragmentShader, 0.4f));
}

TEST(MaterialTests, ReportsMissingAndInvalidMaterialSources) {
    EXPECT_THROW(Assets::LoadMaterial(MaterialFixture("SolidColorLit.slang"), "MissingMaterial"),
                 std::invalid_argument);
    EXPECT_THROW(Assets::LoadMaterial(InvalidFixture("InvalidMaterial.slang"), "InvalidMaterial"),
                 std::runtime_error);
}

TEST(MaterialTests, RejectsUnsupportedAssociatedBase) {
    try {
        (void)Assets::LoadMaterial(InvalidFixture("UnsupportedBase.slang"), "UnsupportedBase");
        FAIL() << "Expected an unsupported associated base to be rejected";
    } catch (const std::invalid_argument& error) {
        EXPECT_NE(std::string(error.what()).find("unsupported IMaterialDefinition.Base"),
                  std::string::npos);
    }
}

TEST(MaterialTests, RejectsTypesThatDoNotImplementMaterialDefinition) {
    try {
        (void)Assets::LoadMaterial(InvalidFixture("NotAMaterial.slang"), "NotAMaterial");
        FAIL() << "Expected a type outside the material contract to be rejected";
    } catch (const std::invalid_argument& error) {
        EXPECT_NE(std::string(error.what()).find("does not satisfy IMaterialDefinition"),
                  std::string::npos);
    }
}

} // namespace
