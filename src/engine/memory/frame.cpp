// =============================================================================
//  engine/memory/frame.cpp  —  FrameAllocator implementation
// =============================================================================
#include "engine/memory/frame.hpp"

namespace mem {

FrameAllocator::FrameAllocator(std::size_t bytes_per_buffer)
    : a_(bytes_per_buffer), b_(bytes_per_buffer), cur_(&a_), prev_(&b_) {}

void FrameAllocator::flip() {
    Arena* tmp = cur_;
    cur_  = prev_;   // the other buffer becomes current…
    prev_ = tmp;     // …and what we just wrote becomes "last frame"
    cur_->reset();   // clear the new current (it held data from two frames ago)
}

} // namespace mem
