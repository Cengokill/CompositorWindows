#ifndef CameraRawPixels_h
#define CameraRawPixels_h
#include <stdint.h>
#include <stddef.h>
// Camera Raw's Light and Color groups on premultiplied RGBA. Amounts use Camera Raw's ranges.
// `clipping` 0 renders the grade; 1 is the highlight-clip view; 2 is the shadow-clip view.
void adjust_camera_raw(uint8_t *rgba, size_t width, size_t height, size_t stride,
                       double redGain, double greenGain, double blueGain, double exposure, double contrast,
                       double highlights, double shadows, double whites, double blacks,
                       double vibrance, double saturation, int clipping);
void adjust_camera_raw_clip_overlay(uint8_t *rgba, size_t width, size_t height, size_t stride, int shadows, int highlights);
void adjust_camera_raw_curve_color(uint8_t *rgba, size_t width, size_t height, size_t stride,
                                   const float *toneLut, const float *redLut, const float *greenLut, const float *blueLut,
                                   double refineSaturation, const float *mixer, int pointCount, const float *points,
                                   const float *grade, double blending, double balance, int visualize);
void adjust_camera_raw_effects(uint8_t *rgba, size_t width, size_t height, size_t stride,
                               double texture, double clarity, double dehaze,
                               double glow, int glowStyle, double glowRange, double glowSpread, double glowWarmth,
                               double vignetteAmount, double vignetteMidpoint, double vignetteRoundness,
                               double vignetteFeather, double vignetteHighlights, int vignetteStyle,
                               double scale);
void adjust_camera_raw_detail(uint8_t *rgba, size_t width, size_t height, size_t stride,
                              double sharpenAmount, double sharpenRadius, double sharpenDetail, double sharpenMasking,
                              double noiseLuminance, double noiseLuminanceDetail, double noiseLuminanceContrast,
                              double noiseColor, double noiseColorDetail, double noiseColorSmoothness, double scale);
void adjust_camera_raw_sharpen_mask_overlay(uint8_t *rgba, size_t width, size_t height, size_t stride,
                                            double sharpenRadius, double sharpenDetail, double sharpenMasking, double scale);
void adjust_camera_raw_optics(uint8_t *rgba, size_t width, size_t height, size_t stride,
                              int removeChromatic, int lensProfile, double profileDistortion, double profileVignetting,
                              double distortionK, double purpleAmount, double purpleHueLow, double purpleHueHigh,
                              double greenAmount, double greenHueLow, double greenHueHigh,
                              double vignetteAmount, double vignetteMidpoint, double scale);
void adjust_camera_raw_calibration(uint8_t *rgba, size_t width, size_t height, size_t stride,
                                   double shadowTint, double redHue, double redSaturation,
                                   double greenHue, double greenSaturation, double blueHue, double blueSaturation,
                                   int processVersion);
#endif
