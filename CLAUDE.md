# Development guidance

Read [CONTRIBUTING.md](CONTRIBUTING.md) for architecture, Tracktion API usage
and invariants before changing code. It is the shared development reference.

- Build and dependency setup: [README.md](README.md#build-from-source).
- User workflows: [MANUAL.md](MANUAL.md).
- Verified coverage and remaining limits: [STATUS.md](STATUS.md).
- Preserve legacy serialized identifiers and migration behavior.
- Keep local credentials, user recordings, sessions and machine-specific notes
  out of commits. Use synthetic or explicitly redistributable test fixtures.
- Report only checks actually performed; distinguish automated tests from native
  interaction and hardware acceptance.
