# UVE Manager 1.1.0 – Changelog

Baseline: v1.0.9. Summary release of the Video2X integration (1.0.7 – 1.0.9) plus a Real-CUGAN fix.

## Fixed

- **Real-CUGAN failed with "the argument for option 'noise-level' is invalid".** Video2X 6.4.0 checks the noise level
  while parsing its arguments and rejects values below 0, so the "Conservative" level (-1) cannot be used from the
  command line although its model files are part of the release. UVE Manager used -1 as the default for Real-CUGAN SE
  and Pro. The noise levels offered are now *No denoise* (default) and *Denoise 1×–3×* where the model provides them;
  a -1 stored in settings or projects by 1.0.8 / 1.0.9 falls back to the default.
- A rejected Video2X argument (exit code -1, shown by Windows as 4294967295) is now reported as such instead of
  "Video2X crashed".

## Included since 1.0.6

- 1.0.7: Video2X + Anime4K v4 as an AI upscale provider, custom GLSL shader library, 5-second exact-pipeline preview.
- 1.0.8: Real-ESRGAN and Real-CUGAN through Video2X (ncnn, Vulkan).
- 1.0.9: Install Video2X as the first step in Settings and Output Settings.

See `CHANGELOG_1.0.7.md`, `CHANGELOG_1.0.8.md` and `CHANGELOG_1.0.9.md` for details.

## Tests

- The mock Video2X CLI rejects negative noise levels like the real 6.4.0 binary; a unit test makes sure no model
  offers one, and argument errors are classified separately from crashes. All unit tests and the self test pass.
