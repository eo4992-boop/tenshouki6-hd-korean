# Specification

## Acceptance checklist

- [ ] Never modify an input game file during analysis.
- [ ] Record file size and SHA-256.
- [ ] Record a bounded header preview and byte-frequency/entropy summary.
- [ ] Extract only clearly identified text candidates; label them as candidates.
- [ ] Emit deterministic UTF-8 JSON.
- [ ] Fail safely for missing files, directories, and unreadable inputs.
- [ ] Do not claim the N6P format is understood until structural evidence supports it.
- [ ] Add automated tests for normal input and failure cases.
- [ ] Keep original game assets out of the repository.

## Non-goals for the first implementation

- No file rewriting.
- No executable patching.
- No automatic translation.
- No destructive operations.
