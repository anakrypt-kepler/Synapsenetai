#include "tui/tui.h"
#include <ncurses.h>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <map>

namespace synapse {
namespace tui {

struct ModelInfo {
    std::string modelId;
    std::string name;
    std::string version;
    std::string architecture;
    uint64_t parameterCount;
    uint64_t sizeBytes;
    std::string quantization;
    std::string status;
    uint64_t loadedAt;
    uint64_t inferenceCount;
    uint64_t avgInferenceMs;
    double accuracy;
    std::string license;
    std::string author;
    bool isLocal;
    bool isVerified;
    bool isEncrypted;
};

struct InferenceRequest {
    std::string requestId;
    std::string modelId;
    std::string inputType;
    uint64_t inputSize;
    uint64_t timestamp;
    uint64_t durationMs;
    std::string status;
    double confidence;
};

struct TrainingJob {
    std::string jobId;
    std::string modelId;
    std::string datasetId;
    uint64_t startedAt;
    uint64_t epoch;
    uint64_t totalEpochs;
    double loss;
    double accuracy;
    std::string status;
    uint64_t samplesProcessed;
    uint64_t totalSamples;
};

struct ModelScreenState {
    std::vector<ModelInfo> models;
    std::vector<InferenceRequest> recentInferences;
    std::vector<TrainingJob> trainingJobs;
    int selectedModel;
    int selectedInference;
    int selectedJob;
    int scrollOffset;
    std::string currentView;
    std::string searchQuery;
    uint64_t totalInferences;
    uint64_t totalModels;
    uint64_t activeModels;
};

static ModelScreenState modelState;

static void initModelState() {
    modelState.models.clear();
    modelState.recentInferences.clear();
    modelState.trainingJobs.clear();
    modelState.selectedModel = 0;
    modelState.selectedInference = 0;
    modelState.selectedJob = 0;
    modelState.scrollOffset = 0;
    modelState.currentView = "overview";
    modelState.searchQuery = "";
    modelState.totalInferences = 0;
    modelState.totalModels = 0;
    modelState.activeModels = 0;
}

static std::string formatBytes(uint64_t bytes) {
    std::stringstream ss;
    if (bytes >= 1024ULL * 1024 * 1024) {
        ss << std::fixed << std::setprecision(2) << (bytes / (1024.0 * 1024 * 1024)) << " GB";
    } else if (bytes >= 1024 * 1024) {
        ss << std::fixed << std::setprecision(2) << (bytes / (1024.0 * 1024)) << " MB";
    } else if (bytes >= 1024) {
        ss << std::fixed << std::setprecision(2) << (bytes / 1024.0) << " KB";
    } else {
        ss << bytes << " B";
    }
    return ss.str();
}

static std::string formatParams(uint64_t params) {
    std::stringstream ss;
    if (params >= 1000000000) {
        ss << std::fixed << std::setprecision(1) << (params / 1000000000.0) << "B";
    } else if (params >= 1000000) {
        ss << std::fixed << std::setprecision(1) << (params / 1000000.0) << "M";
    } else if (params >= 1000) {
        ss << std::fixed << std::setprecision(1) << (params / 1000.0) << "K";
    } else {
        ss << params;
    }
    return ss.str();
}

static std::string formatTimestamp(uint64_t timestamp) {
    time_t t = static_cast<time_t>(timestamp);
    struct tm* tm = localtime(&t);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm);
    return std::string(buf);
}

static std::string shortenModelId(const std::string& modelId) {
    if (modelId.length() <= 16) return modelId;
    return modelId.substr(0, 8) + "..." + modelId.substr(modelId.length() - 8);
}

static void drawModelHeader(WINDOW* win, int width) {
    wattron(win, A_BOLD | COLOR_PAIR(2));
    mvwprintw(win, 1, 2, "SYNAPSENET AI MODELS");
    wattroff(win, A_BOLD | COLOR_PAIR(2));
    
    mvwhline(win, 2, 1, ACS_HLINE, width - 2);
    
    std::stringstream ss;
    ss << "Models: " << modelState.totalModels;
    ss << " | Active: " << modelState.activeModels;
    ss << " | Total Inferences: " << modelState.totalInferences;
    
    mvwprintw(win, 3, 2, "%s", ss.str().c_str());
}

static void drawModelList(WINDOW* win, int startY, int height, int width) {
    wattron(win, A_BOLD);
    mvwprintw(win, startY, 2, "AVAILABLE MODELS");
    wattroff(win, A_BOLD);
    
    mvwhline(win, startY + 1, 1, ACS_HLINE, width - 2);
    
    wattron(win, A_DIM);
    mvwprintw(win, startY + 2, 2, "%-20s %-15s %-12s %-10s %-10s %-12s %-8s", 
              "NAME", "ARCHITECTURE", "PARAMS", "SIZE", "QUANT", "INFERENCES", "STATUS");
    wattroff(win, A_DIM);
    
    mvwhline(win, startY + 3, 1, ACS_HLINE, width - 2);
    
    int y = startY + 4;
    int maxVisible = height - startY - 6;
    
    for (size_t i = modelState.scrollOffset; 
         i < modelState.models.size() && y < startY + maxVisible; 
         i++, y++) {
        const ModelInfo& model = modelState.models[i];
        
        if (static_cast<int>(i) == modelState.selectedModel) {
            wattron(win, A_REVERSE);
        }
        
        mvwprintw(win, y, 2, "%-20s", model.name.c_str());
        mvwprintw(win, y, 22, "%-15s", model.architecture.c_str());
        mvwprintw(win, y, 37, "%-12s", formatParams(model.parameterCount).c_str());
        mvwprintw(win, y, 49, "%-10s", formatBytes(model.sizeBytes).c_str());
        mvwprintw(win, y, 59, "%-10s", model.quantization.c_str());
        mvwprintw(win, y, 69, "%-12lu", model.inferenceCount);
        
        int statusColor = 7;
        if (model.status == "loaded") statusColor = 2;
        else if (model.status == "loading") statusColor = 3;
        else if (model.status == "error") statusColor = 1;
        else if (model.status == "available") statusColor = 6;
        
        wattron(win, COLOR_PAIR(statusColor));
        mvwprintw(win, y, 81, "%-8s", model.status.c_str());
        wattroff(win, COLOR_PAIR(statusColor));
        
        if (model.isVerified) {
            wattron(win, COLOR_PAIR(2));
            mvwprintw(win, y, 89, "[V]");
            wattroff(win, COLOR_PAIR(2));
        }
        
        if (model.isEncrypted) {
            wattron(win, COLOR_PAIR(6));
            mvwprintw(win, y, 92, "[E]");
            wattroff(win, COLOR_PAIR(6));
        }
        
        if (static_cast<int>(i) == modelState.selectedModel) {
            wattroff(win, A_REVERSE);
        }
    }
    
    if (modelState.models.empty()) {
        wattron(win, A_DIM);
        mvwprintw(win, startY + 4, 2, "[GENESIS] No models available");
        wattroff(win, A_DIM);
    }
}

static void drawModelDetails(WINDOW* win, int startY, int width) {
    if (modelState.models.empty()) return;
    if (modelState.selectedModel >= static_cast<int>(modelState.models.size())) return;
    
    const ModelInfo& model = modelState.models[modelState.selectedModel];
    
    wattron(win, A_BOLD);
    mvwprintw(win, startY, 2, "MODEL DETAILS");
    wattroff(win, A_BOLD);
    
    mvwhline(win, startY + 1, 1, ACS_HLINE, width - 2);
    
    int col1 = 2;
    int col2 = width / 2;
    
    mvwprintw(win, startY + 2, col1, "Model ID:      %s", model.modelId.c_str());
    mvwprintw(win, startY + 3, col1, "Name:          %s", model.name.c_str());
    mvwprintw(win, startY + 4, col1, "Version:       %s", model.version.c_str());
    mvwprintw(win, startY + 5, col1, "Architecture:  %s", model.architecture.c_str());
    mvwprintw(win, startY + 6, col1, "Parameters:    %s", formatParams(model.parameterCount).c_str());
    mvwprintw(win, startY + 7, col1, "Size:          %s", formatBytes(model.sizeBytes).c_str());
    
    mvwprintw(win, startY + 2, col2, "Quantization:  %s", model.quantization.c_str());
    mvwprintw(win, startY + 3, col2, "Status:        %s", model.status.c_str());
    mvwprintw(win, startY + 4, col2, "Inferences:    %lu", model.inferenceCount);
    mvwprintw(win, startY + 5, col2, "Avg Latency:   %lu ms", model.avgInferenceMs);
    mvwprintw(win, startY + 6, col2, "Accuracy:      %.2f%%", model.accuracy * 100);
    mvwprintw(win, startY + 7, col2, "Author:        %s", model.author.c_str());
    
    mvwhline(win, startY + 8, 1, ACS_HLINE, width - 2);
    
    mvwprintw(win, startY + 9, col1, "License:       %s", model.license.c_str());
    
    std::string flags;
    if (model.isLocal) flags += "[LOCAL] ";
    if (model.isVerified) flags += "[VERIFIED] ";
    if (model.isEncrypted) flags += "[ENCRYPTED] ";
    
    if (!flags.empty()) {
        mvwprintw(win, startY + 9, col2, "Flags:         %s", flags.c_str());
    }
    
    if (model.loadedAt > 0) {
        mvwprintw(win, startY + 10, col1, "Loaded At:     %s", formatTimestamp(model.loadedAt).c_str());
    }
}

static void drawInferenceList(WINDOW* win, int startY, int height, int width) {
    wattron(win, A_BOLD);
    mvwprintw(win, startY, 2, "RECENT INFERENCES");
    wattroff(win, A_BOLD);
    
    mvwhline(win, startY + 1, 1, ACS_HLINE, width - 2);
    
    wattron(win, A_DIM);
    mvwprintw(win, startY + 2, 2, "%-16s %-20s %-12s %-10s %-10s %-10s %-10s", 
              "REQUEST ID", "MODEL", "INPUT TYPE", "SIZE", "DURATION", "CONFIDENCE", "STATUS");
    wattroff(win, A_DIM);
    
    mvwhline(win, startY + 3, 1, ACS_HLINE, width - 2);
    
    int y = startY + 4;
    int maxVisible = height - startY - 6;
    
    for (size_t i = 0; i < modelState.recentInferences.size() && y < startY + maxVisible; i++, y++) {
        const InferenceRequest& req = modelState.recentInferences[i];
        
        if (static_cast<int>(i) == modelState.selectedInference) {
            wattron(win, A_REVERSE);
        }
        
        mvwprintw(win, y, 2, "%-16s", shortenModelId(req.requestId).c_str());
        mvwprintw(win, y, 18, "%-20s", shortenModelId(req.modelId).c_str());
        mvwprintw(win, y, 38, "%-12s", req.inputType.c_str());
        mvwprintw(win, y, 50, "%-10s", formatBytes(req.inputSize).c_str());
        mvwprintw(win, y, 60, "%lu ms", req.durationMs);
        mvwprintw(win, y, 70, "%.2f%%", req.confidence * 100);
        
        int statusColor = 7;
        if (req.status == "completed") statusColor = 2;
        else if (req.status == "processing") statusColor = 3;
        else if (req.status == "failed") statusColor = 1;
        else if (req.status == "queued") statusColor = 6;
        
        wattron(win, COLOR_PAIR(statusColor));
        mvwprintw(win, y, 80, "%-10s", req.status.c_str());
        wattroff(win, COLOR_PAIR(statusColor));
        
        if (static_cast<int>(i) == modelState.selectedInference) {
            wattroff(win, A_REVERSE);
        }
    }
    
    if (modelState.recentInferences.empty()) {
        wattron(win, A_DIM);
        mvwprintw(win, startY + 4, 2, "[GENESIS] No inference requests");
        wattroff(win, A_DIM);
    }
}

static void drawTrainingJobs(WINDOW* win, int startY, int height, int width) {
    wattron(win, A_BOLD);
    mvwprintw(win, startY, 2, "TRAINING JOBS");
    wattroff(win, A_BOLD);
    
    mvwhline(win, startY + 1, 1, ACS_HLINE, width - 2);
    
    wattron(win, A_DIM);
    mvwprintw(win, startY + 2, 2, "%-16s %-20s %-10s %-10s %-10s %-10s %-10s", 
              "JOB ID", "MODEL", "EPOCH", "LOSS", "ACCURACY", "PROGRESS", "STATUS");
    wattroff(win, A_DIM);
    
    mvwhline(win, startY + 3, 1, ACS_HLINE, width - 2);
    
    int y = startY + 4;
    int maxVisible = height - startY - 6;
    
    for (size_t i = 0; i < modelState.trainingJobs.size() && y < startY + maxVisible; i++, y++) {
        const TrainingJob& job = modelState.trainingJobs[i];
        
        if (static_cast<int>(i) == modelState.selectedJob) {
            wattron(win, A_REVERSE);
        }
        
        mvwprintw(win, y, 2, "%-16s", shortenModelId(job.jobId).c_str());
        mvwprintw(win, y, 18, "%-20s", shortenModelId(job.modelId).c_str());
        mvwprintw(win, y, 38, "%lu/%lu", job.epoch, job.totalEpochs);
        mvwprintw(win, y, 48, "%.4f", job.loss);
        mvwprintw(win, y, 58, "%.2f%%", job.accuracy * 100);
        
        double progress = job.totalSamples > 0 ? 
            (static_cast<double>(job.samplesProcessed) / job.totalSamples) * 100 : 0;
        mvwprintw(win, y, 68, "%.1f%%", progress);
        
        int statusColor = 7;
        if (job.status == "running") statusColor = 2;
        else if (job.status == "paused") statusColor = 3;
        else if (job.status == "failed") statusColor = 1;
        else if (job.status == "completed") statusColor = 6;
        else if (job.status == "queued") statusColor = 7;
        
        wattron(win, COLOR_PAIR(statusColor));
        mvwprintw(win, y, 78, "%-10s", job.status.c_str());
        wattroff(win, COLOR_PAIR(statusColor));
        
        if (static_cast<int>(i) == modelState.selectedJob) {
            wattroff(win, A_REVERSE);
        }
    }
    
    if (modelState.trainingJobs.empty()) {
        wattron(win, A_DIM);
        mvwprintw(win, startY + 4, 2, "[GENESIS] No training jobs");
        wattroff(win, A_DIM);
    }
}

static void drawLoadModelForm(WINDOW* win, int startY, int width) {
    wattron(win, A_BOLD);
    mvwprintw(win, startY, 2, "LOAD MODEL");
    wattroff(win, A_BOLD);
    
    mvwhline(win, startY + 1, 1, ACS_HLINE, width - 2);
    
    mvwprintw(win, startY + 2, 2, "Model Path: [Enter path or URL...]");
    mvwprintw(win, startY + 3, 2, "Model ID:   [Auto-generated]");
    
    mvwhline(win, startY + 4, 1, ACS_HLINE, width - 2);
    
    wattron(win, A_BOLD);
    mvwprintw(win, startY + 5, 2, "LOAD OPTIONS:");
    wattroff(win, A_BOLD);
    
    mvwprintw(win, startY + 6, 2, "[ ] Load to GPU");
    mvwprintw(win, startY + 7, 2, "[X] Verify checksum");
    mvwprintw(win, startY + 8, 2, "[ ] Enable encryption");
    mvwprintw(win, startY + 9, 2, "[ ] Auto-quantize (INT8)");
    
    mvwhline(win, startY + 10, 1, ACS_HLINE, width - 2);
    
    wattron(win, A_BOLD);
    mvwprintw(win, startY + 11, 2, "SUPPORTED FORMATS:");
    wattroff(win, A_BOLD);
    
    mvwprintw(win, startY + 12, 2, "  GGUF, GGML, SafeTensors, PyTorch, ONNX");
    
    mvwhline(win, startY + 13, 1, ACS_HLINE, width - 2);
    
    wattron(win, A_DIM);
    mvwprintw(win, startY + 14, 2, "Press [Enter] to load model");
    wattroff(win, A_DIM);
}

static void drawInferenceForm(WINDOW* win, int startY, int width) {
    wattron(win, A_BOLD);
    mvwprintw(win, startY, 2, "RUN INFERENCE");
    wattroff(win, A_BOLD);
    
    mvwhline(win, startY + 1, 1, ACS_HLINE, width - 2);
    
    mvwprintw(win, startY + 2, 2, "Model:      [Select model...]");
    mvwprintw(win, startY + 3, 2, "Input Type: [Text / Image / Audio / Embedding]");
    mvwprintw(win, startY + 4, 2, "Input:      [Enter input or path...]");
    
    mvwhline(win, startY + 5, 1, ACS_HLINE, width - 2);
    
    wattron(win, A_BOLD);
    mvwprintw(win, startY + 6, 2, "INFERENCE OPTIONS:");
    wattroff(win, A_BOLD);
    
    mvwprintw(win, startY + 7, 2, "Temperature:    [0.7]");
    mvwprintw(win, startY + 8, 2, "Max Tokens:     [512]");
    mvwprintw(win, startY + 9, 2, "Top P:          [0.9]");
    mvwprintw(win, startY + 10, 2, "Top K:          [40]");
    
    mvwhline(win, startY + 11, 1, ACS_HLINE, width - 2);
    
    wattron(win, A_DIM);
    mvwprintw(win, startY + 12, 2, "[GENESIS] No models loaded - cannot run inference");
    wattroff(win, A_DIM);
}

static void drawTrainForm(WINDOW* win, int startY, int width) {
    wattron(win, A_BOLD);
    mvwprintw(win, startY, 2, "START TRAINING");
    wattroff(win, A_BOLD);
    
    mvwhline(win, startY + 1, 1, ACS_HLINE, width - 2);
    
    mvwprintw(win, startY + 2, 2, "Base Model:  [Select model...]");
    mvwprintw(win, startY + 3, 2, "Dataset:     [Select dataset...]");
    mvwprintw(win, startY + 4, 2, "Output Path: [./models/finetuned/]");
    
    mvwhline(win, startY + 5, 1, ACS_HLINE, width - 2);
    
    wattron(win, A_BOLD);
    mvwprintw(win, startY + 6, 2, "TRAINING OPTIONS:");
    wattroff(win, A_BOLD);
    
    mvwprintw(win, startY + 7, 2, "Epochs:         [10]");
    mvwprintw(win, startY + 8, 2, "Batch Size:     [32]");
    mvwprintw(win, startY + 9, 2, "Learning Rate:  [0.0001]");
    mvwprintw(win, startY + 10, 2, "Optimizer:      [AdamW]");
    mvwprintw(win, startY + 11, 2, "LoRA Rank:      [8]");
    
    mvwhline(win, startY + 12, 1, ACS_HLINE, width - 2);
    
    wattron(win, A_BOLD);
    mvwprintw(win, startY + 13, 2, "HARDWARE:");
    wattroff(win, A_BOLD);
    
    mvwprintw(win, startY + 14, 2, "[ ] Use GPU (CUDA)");
    mvwprintw(win, startY + 15, 2, "[ ] Use GPU (Metal)");
    mvwprintw(win, startY + 16, 2, "[X] Use CPU");
    
    mvwhline(win, startY + 17, 1, ACS_HLINE, width - 2);
    
    wattron(win, A_DIM);
    mvwprintw(win, startY + 18, 2, "[GENESIS] Training not available yet");
    wattroff(win, A_DIM);
}

static void drawModelMenu(WINDOW* win, int y, int width) {
    mvwhline(win, y, 1, ACS_HLINE, width - 2);
    
    wattron(win, A_BOLD);
    mvwprintw(win, y + 1, 2, "[O]verview  [M]odels  [D]etails  [I]nference  [T]raining  [L]oad  [Q]uit");
    wattroff(win, A_BOLD);
}

namespace model_screen {

void drawScreen() {
    int maxY, maxX;
    getmaxyx(stdscr, maxY, maxX);
    
    WINDOW* win = newwin(maxY - 2, maxX - 2, 1, 1);
    box(win, 0, 0);
    
    drawModelHeader(win, maxX - 2);
    
    if (modelState.currentView == "overview") {
        drawModelList(win, 5, maxY / 2, maxX - 2);
        drawInferenceList(win, maxY / 2, maxY - 4, maxX - 2);
    } else if (modelState.currentView == "models") {
        drawModelList(win, 5, maxY - 4, maxX - 2);
    } else if (modelState.currentView == "details") {
        drawModelDetails(win, 5, maxX - 2);
    } else if (modelState.currentView == "inference") {
        drawInferenceForm(win, 5, maxX - 2);
        drawInferenceList(win, 25, maxY - 4, maxX - 2);
    } else if (modelState.currentView == "training") {
        drawTrainForm(win, 5, maxX - 2);
        drawTrainingJobs(win, 28, maxY - 4, maxX - 2);
    } else if (modelState.currentView == "load") {
        drawLoadModelForm(win, 5, maxX - 2);
    }
    
    drawModelMenu(win, maxY - 5, maxX - 2);
    
    wrefresh(win);
    delwin(win);
}

int handleInput(int ch) {
    switch (ch) {
        case 'o':
        case 'O':
            modelState.currentView = "overview";
            break;
        case 'm':
        case 'M':
            modelState.currentView = "models";
            break;
        case 'd':
        case 'D':
            modelState.currentView = "details";
            break;
        case 'i':
        case 'I':
            modelState.currentView = "inference";
            break;
        case 't':
        case 'T':
            modelState.currentView = "training";
            break;
        case 'l':
        case 'L':
            modelState.currentView = "load";
            break;
        case KEY_UP:
            if (modelState.selectedModel > 0) {
                modelState.selectedModel--;
                if (modelState.selectedModel < modelState.scrollOffset) {
                    modelState.scrollOffset = modelState.selectedModel;
                }
            }
            break;
        case KEY_DOWN:
            if (modelState.selectedModel < static_cast<int>(modelState.models.size()) - 1) {
                modelState.selectedModel++;
            }
            break;
        case 'q':
        case 'Q':
            return -1;
        case KEY_F(1):
            return 1;
    }
    
    return 0;
}

void init() {
    initModelState();
}

}

}
}
