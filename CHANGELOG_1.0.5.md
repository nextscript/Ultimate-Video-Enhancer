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

- **Updates no longer delete the downloaded tools:** the old uninstaller removed the whole program folder,
  including FFmpeg and REAL-Video-Enhancer (several GB) in `tools\` next to the exe. Since the setup no longer
  runs it, `tools\` survives updates and stays next to the exe. Only when that folder is not writable (e.g.
  `C:\Program Files`) the app uses the tools folder in the user profile, as before.
