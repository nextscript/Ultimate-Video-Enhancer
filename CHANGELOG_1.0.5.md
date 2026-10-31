# UVE Manager 1.0.5 – Changelog

Baseline: v1.0.4.

## Fixed

- **AI Upscaling install failed with "RVE installation failed HTTP 403":** the installer looked up the
  REAL-Video-Enhancer backend through the GitHub API, which allows only 60 requests per hour per internet
  connection without sign-in. When that limit is reached it now reads the public release pages instead, so the
  installation continues. Where a GitHub API limit still stops a download (update check, LUT downloads), the
  message says so and names the time it is lifted instead of a bare "HTTP 403".

- **Setup over an installed version stopped with "UVE Manager cannot be closed":** the update ran the old
  version's uninstaller from the temp folder and closed the app through PowerShell. Behaviour-based virus
  scanners (Bitdefender Advanced Threat Defense) block that, put the uninstaller in quarantine and the setup gave
  up. The setup now closes the app itself without PowerShell and replaces the old program files in place; the old
  uninstaller is no longer started.

## Changed

- Installed copies keep FFmpeg and REAL-Video-Enhancer in `%LOCALAPPDATA%\UVE Manager\tools`, so updates no
  longer lose them. Tools that 1.0.4 kept in the program folder are moved there; when the program folder is on
  another drive they stay where they are and are used from there.
