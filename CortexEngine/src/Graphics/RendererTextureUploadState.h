#pragma once

#include <cstddef>
#include <utility>
#include <vector>

#include "Graphics/TextureUploadQueue.h"
#include "Graphics/TextureUploadReceipt.h"

namespace Cortex::Graphics {

struct TextureUploadRuntimeState {
    std::vector<TextureUploadReceipt> receipts;
    TextureUploadQueueState queue;

    void StoreReceipt(TextureUploadReceipt receipt) {
        receipts.push_back(std::move(receipt));
        constexpr size_t kMaxReceipts = 512;
        if (receipts.size() > kMaxReceipts) {
            receipts.erase(
                receipts.begin(),
                receipts.begin() + static_cast<std::ptrdiff_t>(receipts.size() - kMaxReceipts));
        }
    }

    void Reset() {
        receipts.clear();
        queue = {};
    }
};

} // namespace Cortex::Graphics
