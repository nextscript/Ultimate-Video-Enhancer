# UVE Manager 1.1.1 – Changelog

Baseline: v1.1.0. Fixes for Real-ESRGAN / Real-CUGAN with the real Video2X 6.4.0 build on Windows.

## Fixed

- **Real-ESRGAN and Real-CUGAN failed their automatic test job** with "Vulkan is not usable for Video2X … Cannot open
  video encoder". Found in the Video2X log:
  - The FFV1 encoder inside the Video2X 6.4.0 build does not accept the pixel format `gbrp`. The lossless intermediate
    now uses `bgr0` (lossless RGB that this FFV1 encoder supports). The H.264 RGB fallback stays.
  - With the ncnn models, Video2X can end with a non-zero exit code while shutting down, after it has printed
    "Video processed successfully". This now counts as success; the output is still checked exactly (size and frame
    count) before it is used, and the job log notes it.
  - The error diagnosis only looks at error lines: the info line "Using Vulkan device: …" no longer turns an encoder
    problem into a misleading "Vulkan is not usable" message.

Failed test results of 1.1.0 are not reused – the test job runs again automatically on the next render (Retry).

## Tests

- The mock Video2X CLI now rejects FFV1 + gbrp and can exit non-zero after a successful run, like the real build.
- New unit tests for both cases and for the classification of the real log; the self test renders Real-ESRGAN with
  a shutdown crash after success. All unit tests and the self test pass.
