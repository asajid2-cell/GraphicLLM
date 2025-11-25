#include "LLMService.h"
#include "Prompts.h"
#include <spdlog/spdlog.h>
#include <chrono>
#include <thread>
#include <llama.h>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>
#include <queue>
#include <string_view>
#ifdef _WIN32
#include <windows.h>
#endif

namespace Cortex::LLM {
namespace {
void LlamaLogCallback(ggml_log_level level, const char* text, void* /*user_data*/) {
    // Filter out noisy INFO/DEBUG logs from llama.cpp; keep WARN/ERROR only
    if (!text) return;
    std::string_view msg(text);
    // Trim trailing newlines for cleaner output
    while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r')) {
        msg.remove_suffix(1);
    }
    switch (level) {
        case GGML_LOG_LEVEL_ERROR:
            spdlog::error("llama: {}", msg);
            break;
        case GGML_LOG_LEVEL_WARN:
            spdlog::warn("llama: {}", msg);
            break;
        default:
            break;
    }
}
} // namespace

LLMService::~LLMService() {
    Shutdown();
}

Result<void> LLMService::Initialize(const LLMConfig& config) {
    m_config = config;

    // Initialize llama.cpp backend
    llama_backend_init();
    llama_log_set(LlamaLogCallback, nullptr);

    // If no model specified, use mock mode
    if (config.modelPath.empty()) {
        spdlog::info("LLM Service initialized (MOCK MODE - no model loaded)");
        spdlog::info("  To use real LLM, provide a model path in config");
        return Result<void>::Ok();
    }

    // Load model with new API
    llama_model_params model_params = llama_model_default_params();
    model_params.n_gpu_layers = std::max(0, m_config.gpuLayers);
    m_model = llama_model_load_from_file(config.modelPath.c_str(), model_params);

    if (!m_model) {
        return Result<void>::Err("Failed to load model from: " + config.modelPath);
    }

    // Create context with new API
    llama_context_params ctx_params = llama_context_default_params();
    ctx_params.n_ctx = config.contextSize;
    ctx_params.n_threads = config.threads;
    ctx_params.n_threads_batch = config.threads;

    m_context = llama_init_from_model(static_cast<llama_model*>(m_model), ctx_params);

    if (!m_context) {
        llama_model_free(static_cast<llama_model*>(m_model));
        m_model = nullptr;
        return Result<void>::Err("Failed to create llama context");
    }

    // Get vocab for logging
    const llama_vocab* vocab = llama_model_get_vocab(static_cast<llama_model*>(m_model));

    spdlog::info("LLM Service initialized (model={}, ctx={}, threads={}, vocab={})",
                 config.modelPath, config.contextSize, config.threads, llama_vocab_n_tokens(vocab));

    // Spin up a single background worker for real inference
    m_workerRunning = true;
    m_workerThread = std::thread([this]() { WorkerLoop(); });

    return Result<void>::Ok();
}

void LLMService::Shutdown() {
    m_shuttingDown = true;
    // Stop worker thread early to avoid waiting indefinitely
    bool hadWorker = m_workerRunning.exchange(false);
    if (hadWorker) {
        m_jobCv.notify_all();
    }
    // wait for active jobs to finish
    {
        std::unique_lock<std::mutex> lock(m_waitMutex);
        m_waitCv.wait(lock, [this]() { return m_activeJobs.load() == 0; });
    }

    if (m_context) {
        llama_free(static_cast<llama_context*>(m_context));
        m_context = nullptr;
    }

    if (m_model) {
        llama_model_free(static_cast<llama_model*>(m_model));
        m_model = nullptr;
    }

    // Stop worker thread (if any)
    if (hadWorker && m_workerThread.joinable()) {
        m_workerThread.join();
    }

    llama_backend_free();
    spdlog::info("LLM Service shut down");
}

void LLMService::SubmitPrompt(const std::string& prompt, LLMCallback callback) {
    if (m_shuttingDown.load()) {
        spdlog::warn("LLM is shutting down, request rejected");
        return;
    }

    // Build the full prompt with system instructions
    std::string fullPrompt = Prompts::BuildPrompt(prompt);

    spdlog::debug("LLM Prompt:\n{}", fullPrompt);

    // If no model loaded, use mock responses
    if (!m_model || !m_context) {
        m_isBusy = true;
        m_activeJobs.fetch_add(1);
        std::thread([this, prompt, callback]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));

            LLMResponse response;
            response.success = true;
            response.inferenceTime = 0.5f;

            // Mock responses
            if (prompt.find("red") != std::string::npos && prompt.find("cube") != std::string::npos) {
                response.text = R"({"commands":[{"type":"add_entity","entity_type":"cube","name":"RedCube","position":[2,1,0],"scale":[1,1,1],"color":[1,0,0,1]}]})";
            }
            else if (prompt.find("sphere") != std::string::npos) {
                response.text = R"({"commands":[{"type":"add_entity","entity_type":"sphere","name":"Sphere1","position":[0,1.5,0],"scale":[0.7,0.7,0.7],"color":[0.2,0.8,0.3,1]}]})";
            }
            else {
                response.text = R"({"commands":[]})";
            }

            spdlog::debug("LLM Response (MOCK):\n{}", response.text);

            {
                std::lock_guard<std::mutex> lock(m_callbackMutex);
                m_pendingCallbacks.emplace(callback, response);
            }

            m_isBusy = false;
            m_activeJobs.fetch_sub(1);
            m_waitCv.notify_all();
        }).detach();
        return;
    }

    // Real llama.cpp inference: push to worker queue
    {
        std::lock_guard<std::mutex> lock(m_jobMutex);
        m_jobQueue.emplace(std::move(fullPrompt), std::move(callback));
    }
    m_jobCv.notify_one();
}

void LLMService::ProcessJob(std::string promptCopy, LLMCallback callback) {
    spdlog::info("LLM: worker thread entry");
    if (auto logger = spdlog::default_logger()) logger->flush();

#ifdef _WIN32
    // Lower priority so rendering stays smooth while the LLM runs
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#endif

    std::ostringstream tid;
    tid << std::this_thread::get_id();
    const auto threadId = tid.str();
    const auto hardTimeout = std::chrono::seconds(10);

    auto startTime = std::chrono::high_resolution_clock::now();

    LLMResponse response;
    response.success = false;
    bool finished = false;
    spdlog::info("LLM[{}]: start (chars={})", threadId, promptCopy.size());
    if (m_shuttingDown.load()) {
        response.text = "Error: shutting down";
        {
            std::lock_guard<std::mutex> lock(m_callbackMutex);
            m_pendingCallbacks.emplace(callback, response);
        }
        return;
    }
    auto finish = [this, &finished]() {
        if (!finished) {
            m_isBusy = false;
            m_activeJobs.fetch_sub(1);
            m_waitCv.notify_all();
            finished = true;
        }
    };

    llama_batch prompt_batch{}; // for cleanup on exception
    spdlog::debug("LLM[{}]: pre-tokenize", threadId);
    try {
        llama_model* model = static_cast<llama_model*>(m_model);
        llama_context* ctx = static_cast<llama_context*>(m_context);
        const llama_vocab* vocab = llama_model_get_vocab(model);

        // Reset KV/memory to avoid stale sequences between prompts
        llama_memory_clear(llama_get_memory(ctx), true);

        // Tokenize prompt
        spdlog::debug("LLM[{}]: tokenize", threadId);
        std::vector<llama_token> tokens;
        tokens.resize(promptCopy.size() + 256); // Add extra space
        int n_tokens = llama_tokenize(
            vocab,
            promptCopy.c_str(),
            promptCopy.size(),
            tokens.data(),
            tokens.size(),
            true,  // add_special
            false  // parse_special
        );

        if (n_tokens < 0) {
            tokens.resize(-n_tokens);
            n_tokens = llama_tokenize(
                vocab,
                promptCopy.c_str(),
                promptCopy.size(),
                tokens.data(),
                tokens.size(),
                true,
                false
            );
        }

        if (n_tokens <= 0) {
            spdlog::error("Failed to tokenize prompt");
            response.text = "Error: Tokenization failed";
            finish();
            {
                std::lock_guard<std::mutex> lock(m_callbackMutex);
                m_pendingCallbacks.emplace(callback, response);
            }
            return;
        }

        tokens.resize(n_tokens);
        if (!tokens.empty()) {
            const int previewCount = std::min<int>(n_tokens, 8);
            std::string preview;
            for (int i = 0; i < previewCount; ++i) {
                preview += std::to_string(tokens[i]);
                if (i + 1 < previewCount) preview += ", ";
            }
            spdlog::debug("LLM[{}]: tokenized {} tokens (chars={}) preview=[{}]",
                          threadId, n_tokens, promptCopy.size(), preview);
        } else {
            spdlog::debug("LLM[{}]: tokenized {} tokens (chars={})", threadId, n_tokens, promptCopy.size());
        }

        // Create batch with explicit buffers so logits/seq_id are valid
        // embd = 0 tells llama_batch_init to allocate token buffers (we are token-based, not embedding-based)
        // n_seq_max = 1 (single sequence)
        spdlog::debug("LLM[{}]: batch-init (n_tokens={})", threadId, n_tokens);
        prompt_batch = llama_batch_init(n_tokens, 0, 1);
        if (!prompt_batch.token || !prompt_batch.pos || !prompt_batch.seq_id || !prompt_batch.n_seq_id || !prompt_batch.logits) {
            spdlog::error("LLM[{}]: prompt batch allocation failed (token={}, pos={}, seq_id={}, n_seq_id={}, logits={})",
                          threadId, !!prompt_batch.token, !!prompt_batch.pos, !!prompt_batch.seq_id,
                          !!prompt_batch.n_seq_id, !!prompt_batch.logits);
            response.text = "Error: batch allocation failed";
            llama_batch_free(prompt_batch);
            prompt_batch = {};
            finish();
            {
                std::lock_guard<std::mutex> lock(m_callbackMutex);
                m_pendingCallbacks.emplace(callback, response);
            }
            return;
        }
        std::fill_n(prompt_batch.logits, n_tokens, 0);
        std::fill_n(prompt_batch.n_seq_id, n_tokens, 0);
        for (int i = 0; i < n_tokens; ++i) {
            prompt_batch.token[i] = tokens[i];
            prompt_batch.pos[i] = i;
            prompt_batch.seq_id[i][0] = 0; // first (and only) sequence id
            prompt_batch.n_seq_id[i] = 1;  // one sequence id
            prompt_batch.logits[i] = (i == n_tokens - 1); // request logits for last prompt token
        }
        prompt_batch.n_tokens = n_tokens;

        // Evaluate prompt
        spdlog::debug("LLM[{}]: decode-prompt (n_tokens={})", threadId, prompt_batch.n_tokens);
        int decodeResult = llama_decode(ctx, prompt_batch);
        if (decodeResult != 0) {
            spdlog::error("LLM[{}]: Failed to decode prompt (code={})", threadId, decodeResult);
            response.text = "Error: Decode failed";
            llama_batch_free(prompt_batch);
            prompt_batch = {};
            finish();
            {
                std::lock_guard<std::mutex> lock(m_callbackMutex);
                m_pendingCallbacks.emplace(callback, response);
            }
            return;
        }
        spdlog::debug("LLM[{}]: initial decode complete", threadId);

        // Create sampler for generation
        spdlog::debug("LLM[{}]: sampler-init", threadId);
        llama_sampler_chain_params sparams = llama_sampler_chain_default_params();
        llama_sampler* sampler = llama_sampler_chain_init(sparams);
        llama_sampler_chain_add(sampler, llama_sampler_init_temp(m_config.temperature));
        llama_sampler_chain_add(sampler, llama_sampler_init_penalties(/*last_n*/128, /*repeat*/1.1f, /*freq*/0.0f, /*present*/0.0f));
        llama_sampler_chain_add(sampler, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));
        spdlog::debug("LLM[{}]: sampler initialized", threadId);

        // Generate response tokens
        std::string generatedText;
        int n_decode = 0;
        int n_cur = n_tokens;

        llama_batch next_batch = llama_batch_init(1, 0, 1);
        if (!next_batch.token || !next_batch.pos || !next_batch.seq_id || !next_batch.n_seq_id || !next_batch.logits) {
            spdlog::error("LLM[{}]: next batch allocation failed upfront (token={}, pos={}, seq_id={}, n_seq_id={}, logits={})",
                          threadId, !!next_batch.token, !!next_batch.pos, !!next_batch.seq_id,
                          !!next_batch.n_seq_id, !!next_batch.logits);
            llama_batch_free(next_batch);
            llama_batch_free(prompt_batch);
            prompt_batch = {};
            response.text = "Error: batch allocation failed";
            {
                std::lock_guard<std::mutex> lock(m_callbackMutex);
                m_pendingCallbacks.emplace(callback, response);
            }
            finish();
            return;
        }

        spdlog::debug("LLM[{}]: generate-loop-start", threadId);
        bool generationDecodeFailed = false;
        while (n_decode < m_config.maxTokens) {
            auto now = std::chrono::high_resolution_clock::now();
            if (now - startTime > hardTimeout) {
                spdlog::warn("LLM[{}]: generation timed out after {} tokens", threadId, n_decode);
                break;
            }
            if (n_cur <= 0) break;
            // Use latest logits (-1) as recommended by llama.cpp
            llama_token new_token_id = llama_sampler_sample(sampler, ctx, -1);
            if (new_token_id < 0) {
                spdlog::warn("LLM[{}]: sampler returned invalid token ({}), stopping generation", threadId, new_token_id);
                break;
            }

            // Check for EOS
            if (llama_vocab_is_eog(vocab, new_token_id)) {
                spdlog::debug("LLM[{}]: EOS reached after {} tokens", threadId, n_decode);
                break;
            }

            // Convert token to text (query size first to avoid overflow)
            int needed = llama_token_to_piece(vocab, new_token_id, nullptr, 0, 0, false);
            if (needed > 0) {
                // +4 slack to avoid any off-by-one/null terminator writes from the backend
                std::vector<char> buf(static_cast<size_t>(needed + 4));
                int wrote = llama_token_to_piece(vocab, new_token_id, buf.data(), static_cast<int>(buf.size()), 0, false);
                if (wrote > 0) {
                    generatedText.append(buf.data(), static_cast<size_t>(wrote));
                }
            }

            // Prepare next batch with single token, request logits
            // embd = 0 to allocate token buffers
            std::fill_n(next_batch.logits, 1, 0);
            std::fill_n(next_batch.n_seq_id, 1, 0);
            next_batch.token[0] = new_token_id;
            next_batch.pos[0] = n_cur;
            next_batch.seq_id[0][0] = 0;
            next_batch.n_seq_id[0] = 1;
            next_batch.logits[0] = true;
            next_batch.n_tokens = 1;

            n_decode++;
            n_cur++;

            // Decode
            if (llama_decode(ctx, next_batch) != 0) {
                spdlog::warn("LLM[{}]: decode failed at token {} (id={}, pos={})", threadId, n_decode, new_token_id, n_cur);
                generationDecodeFailed = true;
                break;
            }

            // Stop if we see a complete JSON object
            if (generatedText.find("}]}") != std::string::npos) {
                spdlog::debug("LLM[{}]: detected end of JSON after {} tokens", threadId, n_decode);
                break;
            }

            if ((n_decode % 16) == 0) {
                spdlog::debug("LLM[{}]: generated {} tokens...", threadId, n_decode);
            }
        }

        // Free sampler and batches
        llama_sampler_free(sampler);
        llama_batch_free(next_batch);
        llama_batch_free(prompt_batch);
        prompt_batch = {};
        spdlog::debug("LLM[{}]: generation loop ended after {} tokens", threadId, n_decode);

        auto endTime = std::chrono::high_resolution_clock::now();
        std::chrono::duration<float> elapsed = endTime - startTime;

        // Try to trim to the first/last brace to improve JSON parsing resilience
        auto startPos = generatedText.find('{');
        auto endPos = generatedText.rfind('}');
        if (!generationDecodeFailed) {
            // Prefer JSON if we see balanced braces
            if (startPos != std::string::npos && endPos != std::string::npos && endPos > startPos) {
                response.text = generatedText.substr(startPos, endPos - startPos + 1);
                response.success = true;
            } else {
                // Trim whitespace to decide if we got anything meaningful
                auto first = generatedText.find_first_not_of(" \t\r\n");
                auto last = generatedText.find_last_not_of(" \t\r\n");
                if (first != std::string::npos && last != std::string::npos) {
                    response.text = generatedText.substr(first, last - first + 1);
                    response.success = true; // accept raw text even if not JSON
                } else if (n_decode > 0) {
                    // Generated tokens but only whitespace/control pieces; treat as empty but non-fatal
                    response.text = "";
                    response.success = true;
                } else {
                    response.text = "Error: Empty generation";
                    response.success = false;
                }
            }
        } else {
            response.text = "Error: Decode failed during generation";
            response.success = false;
        }
        response.inferenceTime = elapsed.count();

        if (response.success) {
            spdlog::info("LLM[{}]: success tokens={} elapsed={:.2f}s text_preview=\"{}\"",
                         threadId, n_decode, response.inferenceTime,
                         response.text.substr(0, std::min<size_t>(response.text.size(), 96)));
        } else {
            spdlog::warn("LLM[{}]: fail reason=\"{}\" tokens={} elapsed={:.2f}s",
                         threadId, response.text, n_decode, response.inferenceTime);
        }

        {
            std::lock_guard<std::mutex> lock(m_callbackMutex);
            m_pendingCallbacks.emplace(callback, response);
        }
        finish();
    } catch (const std::exception& ex) {
        spdlog::error("LLM[{}] thread exception: {}", threadId, ex.what());
        response.text = "Error: Exception during inference";
        response.success = false;
        if (prompt_batch.token) {
            llama_batch_free(prompt_batch);
            prompt_batch = {};
        }
        finish();
        {
            std::lock_guard<std::mutex> lock(m_callbackMutex);
            m_pendingCallbacks.emplace(callback, response);
        }
    } catch (...) {
        spdlog::error("LLM[{}] thread unknown exception", threadId);
        response.text = "Error: Unknown exception";
        response.success = false;
        if (prompt_batch.token) {
            llama_batch_free(prompt_batch);
            prompt_batch = {};
        }
        finish();
        {
            std::lock_guard<std::mutex> lock(m_callbackMutex);
            m_pendingCallbacks.emplace(callback, response);
        }
    }

    if (auto logger = spdlog::default_logger()) logger->flush();
}
void LLMService::PumpCallbacks() {
    std::queue<std::pair<LLMCallback, LLMResponse>> local;
    {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        std::swap(local, m_pendingCallbacks);
    }
    while (!local.empty()) {
        auto pair = std::move(local.front());
        local.pop();
        if (pair.first) {
            pair.first(pair.second);
        }
    }
}

std::string LLMService::GetModelInfo() const {
    if (!m_model) {
        return "Mock LLM (no model loaded)";
    }
    return "Model: " + m_config.modelPath + " (llama.cpp)";
}

void LLMService::WorkerLoop() {
    while (true) {
        std::pair<std::string, LLMCallback> job;
        {
            std::unique_lock<std::mutex> lock(m_jobMutex);
            m_jobCv.wait(lock, [this]() { return !m_workerRunning.load() || !m_jobQueue.empty(); });
            if (!m_workerRunning.load() && m_jobQueue.empty()) {
                break;
            }
            job = std::move(m_jobQueue.front());
            m_jobQueue.pop();
        }

        m_isBusy = true;
        m_activeJobs.fetch_add(1);
        ProcessJob(std::move(job.first), std::move(job.second));
    }
}

} // namespace Cortex::LLM
