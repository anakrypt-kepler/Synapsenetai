#include <gtest/gtest.h>
#include "model/model_loader.h"
#include "utils/config.h"
#include <cstdint>
#include <filesystem>
#include <fstream>

using namespace synapse::model;
using namespace synapse::utils;

class ModelLoaderTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create temporary test directory
        testDir = std::filesystem::temp_directory_path() / "synapsenet_test_models";
        std::filesystem::create_directories(testDir);
        
        // Create a minimal mock GGUF file for testing
        mockModelPath = testDir / "test_model.gguf";
        createMockGGUF(mockModelPath.string());
        
        loader = std::make_unique<ModelLoader>();
    }
    
    void TearDown() override {
        if (std::filesystem::exists(testDir)) {
            std::filesystem::remove_all(testDir);
        }
    }
    
    void createMockGGUF(const std::string& path) {
        std::ofstream file(path, std::ios::binary);
        // Write minimal GGUF header
        file.write("GGUF", 4);  // Magic
        file.write("\x03\x00\x00\x00", 4);  // Version 3
        file.write("\x00\x00\x00\x00", 4);  // Metadata KV count = 0
        file.write("\x00\x00\x00\x00", 4);  // Data offset = 0
        // Pad to at least 1KB for testing
        std::string padding(1024, '\0');
        file.write(padding.c_str(), padding.size());
        file.close();
    }
    
    std::filesystem::path testDir;
    std::filesystem::path mockModelPath;
    std::unique_ptr<ModelLoader> loader;
};

// Task 5: Unit Tests for ModelLoader

TEST_F(ModelLoaderTest, ValidateModel) {
    // Should validate correct GGUF file
    EXPECT_TRUE(ModelLoader::validateModel(mockModelPath.string()));
    
    // Should reject non-existent file
    EXPECT_FALSE(ModelLoader::validateModel("/nonexistent/path.gguf"));
}

TEST_F(ModelLoaderTest, DetectFormat) {
    EXPECT_EQ(ModelLoader::detectFormat("model.gguf"), ModelFormat::GGUF);
    EXPECT_EQ(ModelLoader::detectFormat("model.ggml"), ModelFormat::GGML);
    EXPECT_EQ(ModelLoader::detectFormat("model.bin"), ModelFormat::GGML);
    EXPECT_EQ(ModelLoader::detectFormat("model.safetensors"), ModelFormat::SAFETENSORS);
    EXPECT_EQ(ModelLoader::detectFormat("model.pt"), ModelFormat::PYTORCH);
    EXPECT_EQ(ModelLoader::detectFormat("model.onnx"), ModelFormat::ONNX);
    EXPECT_EQ(ModelLoader::detectFormat("model.unknown"), ModelFormat::UNKNOWN);
}

TEST_F(ModelLoaderTest, EstimateMemory) {
    uint64_t estimated = ModelLoader::estimateMemory(mockModelPath.string(), 2048);
    // Should be non-zero
    EXPECT_GT(estimated, 0);
    // Should account for file size + context buffers
    uint64_t fileSize = std::filesystem::file_size(mockModelPath);
    EXPECT_GT(estimated, fileSize);
}

TEST_F(ModelLoaderTest, FormatBytes) {
    EXPECT_EQ(ModelLoader::formatBytes(512), "512.00 B");
    EXPECT_EQ(ModelLoader::formatBytes(1024), "1.00 KB");
    EXPECT_EQ(ModelLoader::formatBytes(1024 * 1024), "1.00 MB");
    EXPECT_EQ(ModelLoader::formatBytes(1024 * 1024 * 1024), "1.00 GB");
}

TEST_F(ModelLoaderTest, GetState) {
    // Initial state should be UNLOADED
    EXPECT_EQ(loader->getState(), ModelState::UNLOADED);
    EXPECT_FALSE(loader->isLoaded());
}

TEST_F(ModelLoaderTest, GetError) {
    std::string error = loader->getError();
    EXPECT_EQ(error, "");  // Should be empty initially
    
    // Try loading non-existent model
    loader->load("/nonexistent/model.gguf");
    EXPECT_NE(loader->getError(), "");
    EXPECT_EQ(loader->getState(), ModelState::ERROR);
}

TEST_F(ModelLoaderTest, ListModels) {
    // Create a few test models
    for (int i = 0; i < 3; i++) {
        createMockGGUF((testDir / ("model_" + std::to_string(i) + ".gguf")).string());
    }
    
    auto models = loader->listModels(testDir.string());
    EXPECT_EQ(models.size(), 4);  // 3 new + 1 from setUp
}

TEST_F(ModelLoaderTest, ConfigSaveLoad) {
    auto& config = Config::instance();
    
    // Test model persistence config
    config.setLastModelPath("/path/to/model.gguf");
    config.setLastModelId("test-model");
    config.setLastModelFormat("GGUF");
    config.setAutoLoadModel(true);
    
    EXPECT_EQ(config.getLastModelPath(), "/path/to/model.gguf");
    EXPECT_EQ(config.getLastModelId(), "test-model");
    EXPECT_EQ(config.getLastModelFormat(), "GGUF");
    EXPECT_TRUE(config.getAutoLoadModel());
}

TEST_F(ModelLoaderTest, ConfigScanPaths) {
    auto& config = Config::instance();
    
    std::vector<std::string> paths = {
        "~/.synapsenet/models",
        "./models",
        "/custom/models"
    };
    
    config.setModelScanPaths(paths);
    auto retrieved = config.getModelScanPaths();
    EXPECT_EQ(retrieved.size(), 3);
    EXPECT_EQ(retrieved[0], paths[0]);
}

TEST_F(ModelLoaderTest, AddRemoveModelScanPath) {
    auto& config = Config::instance();
    config.setModelScanPaths({});  // Clear first
    
    config.addModelScanPath("/path1");
    config.addModelScanPath("/path2");
    
    auto paths = config.getModelScanPaths();
    EXPECT_EQ(paths.size(), 2);
    
    config.removeModelScanPath("/path1");
    paths = config.getModelScanPaths();
    EXPECT_EQ(paths.size(), 1);
    EXPECT_EQ(paths[0], "/path2");
}

TEST_F(ModelLoaderTest, SetGetLoaderConfig) {
    LoaderConfig config;
    config.contextSize = 4096;
    config.threads = 8;
    config.gpuLayers = 32;
    config.useMmap = true;
    
    loader->setConfig(config);
    auto retrieved = loader->getConfig();
    
    EXPECT_EQ(retrieved.contextSize, 4096);
    EXPECT_EQ(retrieved.threads, 8);
    EXPECT_EQ(retrieved.gpuLayers, 32);
    EXPECT_TRUE(retrieved.useMmap);
}

TEST_F(ModelLoaderTest, SetContextSize) {
    loader->setContextSize(4096);
    auto config = loader->getConfig();
    EXPECT_EQ(config.contextSize, 4096);
}

TEST_F(ModelLoaderTest, SetThreads) {
    loader->setThreads(16);
    auto config = loader->getConfig();
    EXPECT_EQ(config.threads, 16);
}

TEST_F(ModelLoaderTest, SetGpuLayers) {
    loader->setGpuLayers(48);
    auto config = loader->getConfig();
    EXPECT_EQ(config.gpuLayers, 48);
}

// Task 5: Integration Tests with mock inference

TEST_F(ModelLoaderTest, StateCallbacks) {
    std::vector<ModelState> stateChanges;
    loader->onStateChange([&stateChanges](ModelState state) {
        stateChanges.push_back(state);
    });
    
    // State should change when calling methods (even if they fail)
    loader->load("/nonexistent.gguf");
    
    // Should have recorded at least LOADING -> ERROR transition
    EXPECT_GT(stateChanges.size(), 0);
}

TEST_F(ModelLoaderTest, ErrorCallback) {
    std::string lastError;
    loader->onError([&lastError](const std::string& err) {
        lastError = err;
    });
    
    loader->load("/nonexistent.gguf");
    
    EXPECT_NE(lastError, "");
    EXPECT_TRUE(lastError.find("not found") != std::string::npos);
}

TEST_F(ModelLoaderTest, ProgressCallback) {
    std::vector<double> progressValues;
    loader->onProgress([&progressValues](double progress) {
        progressValues.push_back(progress);
    });
    
    // Progress callback would be called during load (if model loads successfully)
    // For now just verify setup works
    EXPECT_TRUE(progressValues.empty());
}

// Task 5: Mock generation tests (without actual llama.cpp)

TEST_F(ModelLoaderTest, GenerateWithoutModel) {
    // Should handle gracefully when model not loaded
    std::string result = loader->generate("Hello world");
    EXPECT_NE(result, "");  // Should contain error message
    EXPECT_TRUE(result.find("Error") != std::string::npos || 
               result.find("not loaded") != std::string::npos);
}

TEST_F(ModelLoaderTest, TokenCount) {
    // Should estimate token count (implementation dependent)
    uint32_t count = loader->tokenCount("Hello world");
    EXPECT_GT(count, 0);
}

TEST_F(ModelLoaderTest, DeleteModel) {
    std::string tempModel = (testDir / "delete_me.gguf").string();
    createMockGGUF(tempModel);
    
    EXPECT_TRUE(std::filesystem::exists(tempModel));
    EXPECT_TRUE(loader->deleteModel(tempModel));
    EXPECT_FALSE(std::filesystem::exists(tempModel));
}

// Integration test: Config persistence
TEST_F(ModelLoaderTest, ConfigPersistence) {
    auto& config = Config::instance();
    
    std::string configPath = (testDir / "test.conf").string();
    
    // Set values
    config.setLastModelPath("/test/model.gguf");
    config.setLastModelId("test-id");
    
    // Save
    config.save(configPath);
    
    // Verify file exists and contains expected data
    EXPECT_TRUE(std::filesystem::exists(configPath));
    
    std::ifstream file(configPath);
    std::string content((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());
    EXPECT_TRUE(content.find("model.last_path") != std::string::npos);
}
