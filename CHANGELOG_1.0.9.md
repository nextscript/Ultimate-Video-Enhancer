# UVE Manager 1.0.9 – Changelog

Baseline: v1.0.8.

## Changed

- **Video2X: Install is the first step.** When Video2X is not installed yet, Settings → AI Upscaling → Video2X now always
  shows **Install Video2X 6.4.0** as the main button with a short explanation – also while the status is still loading
  or when loading it failed (the error is shown). Before, the install button depended on the loaded status, so only
  “Select existing executable…” could be visible at first.
- **Output Settings → AI Upscale → Video2X** has its own **Install Video2X** button with progress and Cancel, so the
  provider can be set up without leaving the export panel.
- Using an own Video2X build is now the secondary button **Use my own video2x.exe…** (advanced).

No change to rendering, the Video2X processors, RVE or project files.
