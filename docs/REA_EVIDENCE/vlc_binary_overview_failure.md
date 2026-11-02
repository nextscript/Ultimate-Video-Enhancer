# vlc.exe – Tiefenanalyse (REA `open_binary` / `binary_overview`)

- `open_binary` erfolgreich: format `pe`, kind `executable`, architecture `x86_64`,
  SHA-256 `63d0d02df6e466d642b824dc66be6b5a134bf98b26747ffac8ec0d879dc180d7`.
- `binary_overview` **fehlgeschlagen** mit `provider_unavailable` (retryable). Rückmeldung von REA:

| Provider | Grund |
|---|---|
| ghidra | `not_configured` – `GHIDRA_INSTALL_DIR` nicht gesetzt, keine Java-Laufzeit gefunden |
| hopper | `unsupported_host` – nicht unter Windows |
| ida | `not_configured` – `REA_IDA_MCP_CONFIG` nicht gesetzt |
| kuna | `not_configured` bzw. `target_format_unsupported` – PE nicht zugelassen |

Folge: Keine Disassemblierung, keine Prozedurliste, keine Dekompilierung von VLC durch REA. Ersatzweise wurden
die Import-/Exporttabellen mit `pefile` ausgewertet (pe_imports_exports.md). Siehe docs/REA_LIMITATIONS.md.
