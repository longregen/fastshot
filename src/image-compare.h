#ifndef IMAGE_COMPARE_H
#define IMAGE_COMPARE_H

#include <stdint.h>

// Calculate Mean Squared Error between two BGRA images
// Returns MSE normalized to 0-1 range (0 = identical, 1 = completely different)
float calculate_mse_bgra(const uint8_t *img1, const uint8_t *img2, 
                         uint32_t width, uint32_t height, 
                         uint32_t stride1, uint32_t stride2);

// Convert MSE to similarity score (1 - MSE)
static inline float mse_to_similarity(float mse) {
    return 1.0f - mse;
}

#endif // IMAGE_COMPARE_H