# Local configuration

- `src/config.h` is a private local configuration file containing Wi-Fi credentials. Do not read, search, or display its contents.
- Always exclude `src/config.h` from repository-wide searches and file collection. Do not retrieve its contents indirectly through copies or build artifacts.
- Use `src/config.example.h` to inspect or change configuration options and defaults. When local configuration changes are needed, tell the user what to change.
- Do not add `src/config.h` to Git tracking. Never include credentials in logs, responses, or commits.
