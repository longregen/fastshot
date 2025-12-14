#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include "image-compare.h"

#define BGRA_CHANNELS 4

// Test constants
const uint32_t TEST_WIDTH = 1920;
const uint32_t TEST_HEIGHT = 1080;

static void test_identical_images() {
    printf("Test 1: Identical images... ");
    
    // Calculate stride (must be same for both images in fastshot-loop)
    uint32_t stride = TEST_WIDTH * BGRA_CHANNELS;
    size_t img_size = stride * TEST_HEIGHT;
    
    uint8_t *img1 = malloc(img_size);
    uint8_t *img2 = malloc(img_size);
    
    // Fill with same random data
    for (size_t i = 0; i < img_size; i++) {
        img1[i] = i % 256;
        img2[i] = img1[i];
    }
    
    float mse = calculate_mse_bgra(img1, img2, TEST_WIDTH, TEST_HEIGHT, stride, stride);
    assert(fabs(mse) < 1e-6);
    
    free(img1);
    free(img2);
    printf("PASSED (MSE: %.9f)\n", mse);
}

static void test_completely_different() {
    printf("Test 2: Completely different images... ");
    
    uint32_t stride = TEST_WIDTH * BGRA_CHANNELS;
    size_t img_size = stride * TEST_HEIGHT;
    
    uint8_t *img1 = malloc(img_size);
    uint8_t *img2 = malloc(img_size);
    
    // Fill with opposite values (0 vs 255 in all channels)
    memset(img1, 0, img_size);
    memset(img2, 255, img_size);
    
    float mse = calculate_mse_bgra(img1, img2, TEST_WIDTH, TEST_HEIGHT, stride, stride);
    assert(fabs(mse - 1.0) < 1e-6);
    
    free(img1);
    free(img2);
    printf("PASSED (MSE: %.6f)\n", mse);
}

static void test_different_dimensions() {
    printf("Test 3: Different dimensions rejection... ");
    
    // Test different strides
    uint32_t stride1 = TEST_WIDTH * BGRA_CHANNELS;
    uint32_t stride2 = (TEST_WIDTH + 10) * BGRA_CHANNELS;
    
    uint8_t *img1 = malloc(stride1 * TEST_HEIGHT);
    uint8_t *img2 = malloc(stride2 * TEST_HEIGHT);
    
    float mse = calculate_mse_bgra(img1, img2, TEST_WIDTH, TEST_HEIGHT, stride1, stride2);
    assert(mse == -1.0f); // Should return error
    
    free(img1);
    free(img2);
    printf("PASSED (correctly rejected)\n");
}

int main() {
    printf("Running image comparison tests...\n\n");
    
    test_identical_images();
    test_completely_different();
    test_different_dimensions();
    
    printf("\nAll tests passed!\n");
    return 0;
}