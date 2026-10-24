# odc-onboard-v2 Documentation

| Document | Description |
|----------|-------------|
| [ARCHITECTURE.md](ARCHITECTURE.md) | Full graph architecture, YAML schema, IPC API, and phased implementation plan |
| [SYSTEM_DIAGRAMS.md](SYSTEM_DIAGRAMS.md) | Mermaid workflows: event schema, component interactions, state machines, data paths (17 diagrams) |

## Quick Links

- Example config: [`../config/system.example.yaml`](../config/system.example.yaml)
- v1 reference: [`../../odc-onboard/`](../../odc-onboard/) — maintenance mode; new deployments should use v2
- Config migration: [`../tools/migrate_v1_config.py`](../tools/migrate_v1_config.py)
- YAML field reference: [YAML_SCHEMA.md](YAML_SCHEMA.md)
- Real hardware integration tests: [REAL_HARDWARE_TEST_SUITE.md](REAL_HARDWARE_TEST_SUITE.md)
- DSPatch framework: https://github.com/cross-platform/dspatch
