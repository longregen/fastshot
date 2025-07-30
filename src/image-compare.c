#include "image-compare.h"
#include <libavutil/imgutils.h>

#define BGRA_CHANNELS 4

float calculate_mse_bgra(const uint8_t *img1, const uint8_t *img2, 
                         uint32_t width, uint32_t height, 
                         uint32_t stride1, uint32_t stride2) {
    // Validate inputs
    if (!img1 || !img2) {
        return -1.0f; // Error: null pointer
    }
    
    // For fastshot-loop, we require identical dimensions
    if (stride1 != stride2) {
        return -1.0f; // Error: different strides
    }
    
    // Ensure stride is large enough for the width
    if (stride1 < width * BGRA_CHANNELS) {
        return -1.0f; // Error: stride too small
    }
    
    uint64_t sse = 0;
    size_t pixel_count = 0;
    
    // Process all pixels
    for (uint32_t y = 0; y < height; y++) {
        const uint8_t *row1 = img1 + y * stride1;
        const uint8_t *row2 = img2 + y * stride2;
        
        // Process each pixel's BGRA channels
        for (uint32_t x = 0; x < width; x++) {
            for (int c = 0; c < BGRA_CHANNELS; c++) {
                int diff = row1[x * BGRA_CHANNELS + c] - row2[x * BGRA_CHANNELS + c];
                sse += (uint64_t)(diff * diff);
            }
        }
    }
    
    pixel_count = (size_t)width * height * BGRA_CHANNELS;
    
    // Convert SSE to MSE (normalized to 0-1 range)
    if (pixel_count == 0) return 0.0f;
    return (float)sse / (pixel_count * 255.0f * 255.0f);
}