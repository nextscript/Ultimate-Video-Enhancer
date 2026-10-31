# UVE Manager 1.0.5 – Changelog

Baseline: v1.0.4.

## Fixed

- **AI Upscaling install failed with "RVE installation failed HTTP 403":** the installer looked up the
  REAL-Video-Enhancer backend through the GitHub API, which allows only 60 requests per hour per internet
  connection without sign-in. When that limit is reached it now reads the public release pages instead, so the
  installation continues. Where a GitHub API limit still stops a download (update check, LUT downloads), the
  message says so and names the time it is lifted instead of a bare "HTTP 403".
