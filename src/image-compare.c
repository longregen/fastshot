#include "image-compare.h"
#include <stddef.h>
#include <stdint.h>

#define BGRA_CHANNELS 4

float calculate_mse_bgra(const uint8_t *img1, const uint8_t *img2, 
                         uint32_t width, uint32_t height, 
                         uint32_t stride1, uint32_t stride2) {
    // Validate inputs
    if (!img1 || !img2) {
        return -1.0f; // Error: null pointer
    }
    
    // Validate dimensions to prevent overflow
    if (width == 0 || height == 0) {
        return -1.0f; // Error: invalid dimensions
    }
    
    // Check for potential integer overflow in pixel count calculation
    if (width > UINT32_MAX / height || 
        (uint64_t)width * height > SIZE_MAX / BGRA_CHANNELS) {
        return -1.0f; // Error: dimensions too large
    }
    
    // For fastshot-loop, we require identical dimensions
    if (stride1 != stride2) {
        return -1.0f; // Error: different strides
    }
    
    // Ensure stride is large enough for the width and check for overflow
    if (width > UINT32_MAX / BGRA_CHANNELS || 
        stride1 < width * BGRA_CHANNELS || 
        stride2 < width * BGRA_CHANNELS) {
        return -1.0f; // Error: stride too small or overflow
    }
    
    // Additional safety check for stride overflow in loop
    if (height > 0 && (stride1 > SIZE_MAX / height || stride2 > SIZE_MAX / height)) {
        return -1.0f; // Error: stride * height would overflow
    }
    
    uint64_t sse = 0;
    size_t pixel_count = 0;
    
    // Process all pixels with bounds checking
    for (uint32_t y = 0; y < height; y++) {
        // Check for potential pointer arithmetic overflow
        if ((size_t)y > SIZE_MAX / stride1 || (size_t)y > SIZE_MAX / stride2) {
            return -1.0f; // Error: row offset would overflow
        }
        
        const uint8_t *row1 = img1 + (size_t)y * stride1;
        const uint8_t *row2 = img2 + (size_t)y * stride2;
        
        // Process each pixel's BGRA channels, but be more conservative
        uint32_t safe_width = width;
        
        // Ensure we don't read beyond available data for this row
        size_t max_pixels_row1 = stride1 / BGRA_CHANNELS;
        size_t max_pixels_row2 = stride2 / BGRA_CHANNELS;
        
        if (safe_width > max_pixels_row1) safe_width = max_pixels_row1;
        if (safe_width > max_pixels_row2) safe_width = max_pixels_row2;
        
        for (uint32_t x = 0; x < safe_width; x++) {
            // Check for potential offset overflow
            if (x > UINT32_MAX / BGRA_CHANNELS) {
                return -1.0f; // Error: pixel offset would overflow
            }
            
            size_t offset = (size_t)x * BGRA_CHANNELS;
            
            // Double-check bounds (should be safe now but be extra careful)
            if (offset + BGRA_CHANNELS > stride1 || offset + BGRA_CHANNELS > stride2) {
                break; // Stop processing this row if we hit bounds
            }
            
            for (int c = 0; c < BGRA_CHANNELS; c++) {
                int diff = (int)row1[offset + c] - (int)row2[offset + c];
                sse += (uint64_t)(diff * diff);
                
                // Check for SSE overflow (very unlikely but possible)
                if (sse < (uint64_t)(diff * diff)) {
                    return -1.0f; // Error: SSE overflow
                }
            }
        }
        
        // If we processed fewer pixels than expected, account for this
        if (safe_width < width) {
            // Fill in missing pixels as maximum difference
            uint64_t missing_pixels = (width - safe_width);
            sse += missing_pixels * BGRA_CHANNELS * (255 * 255);
        }
    }
    
    // Calculate pixel count with overflow check (already validated above)
    pixel_count = (size_t)width * height * BGRA_CHANNELS;
    
    // Convert SSE to MSE (normalized to 0-1 range)
    if (pixel_count == 0) {
        return 0.0f;
    }
    
    // Avoid potential division issues
    double denominator = (double)pixel_count * 255.0 * 255.0;
    if (denominator <= 0.0) {
        return -1.0f; // Error: invalid denominator
    }
    
    double mse = (double)sse / denominator;
    
    // Ensure result is within expected range
    if (mse < 0.0 || mse > 1.0) {
        return -1.0f; // Error: MSE out of valid range
    }
    
    return (float)mse;
}