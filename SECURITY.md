# Security

## Security Posture

Lua xjson trusts callers and the Lua runtime. JSON input may be attacker-controlled. Lua xjson is
not designed to isolate mutually untrusted parties. Applications are responsible for access control,
process isolation, flag selection, and resource limits. Reports that require a violation of these
assumptions are outside the project's security scope, but may still be considered as ordinary
robustness or correctness issues.
