# Falafel Repository Instructions

## Mandatory Documentation and Verification Policy

Every development task must include a final verification and documentation phase.

This policy applies to all future changes involving application logic, firmware,
networking, protocols, persistence, GUI, build configuration, tests, architecture,
or project structure.

### 1. Implementation

Complete the requested development task before updating the documentation.

Do not consider a task completed merely because the code compiles.

### 2. Verification

After implementation:

- Run all relevant automated tests.
- Compile the affected components.
- Run integration tests when applicable.
- Run sanitizer tests when relevant and available.
- Check for regressions.
- Execute `git diff --check`.
- Verify that the implementation satisfies the original requirements.

If verification fails, attempt to fix the problems and repeat the relevant checks.

Never report tests as successful unless they were actually executed.

### 3. Mandatory documentation update

After implementation and verification, review all existing project documentation.

Identify which documents are affected by the changes.

Update every relevant document to reflect the actual implementation.

This includes, where applicable:

- `README.md`
- Architecture documentation
- Protocol specifications
- Firmware documentation
- Simulator documentation
- Relay documentation
- Build and installation instructions
- Testing documentation
- Development roadmap
- Known limitations
- Configuration instructions

Documentation must describe the actual state of the code, not planned or
hypothetical functionality.

Remove or correct outdated information.

Do not duplicate information unnecessarily.

Do not rewrite unrelated documentation.

Preserve the existing documentation structure unless a change is justified.

### 4. Project status tracking

Maintain a document at:

`docs/PROJECT_STATUS.md`

Create it if it does not exist.

Keep it updated after every completed development task.

It must contain:

- Current project status
- Implemented features
- Verified functionality
- Current architecture
- Known bugs and limitations
- Pending tasks
- Next recommended development steps
- Last completed milestone

Clearly distinguish between implemented, tested, partially implemented, and
planned functionality.

### 5. Development changelog

Maintain:

`docs/CHANGELOG.md`

Record meaningful completed changes.

For each development session, document:

- Date
- Components modified
- Features implemented
- Bugs fixed
- Tests performed
- Relevant architectural decisions

Avoid recording trivial formatting changes unless they are significant.

### 6. Final consistency check

After updating the documentation:

1. Verify that documentation matches the actual code.
2. Verify that completed features are not still described as pending.
3. Verify that planned features are not described as implemented.
4. Check that commands and file paths are correct.
5. Check that architectural diagrams remain accurate.
6. Verify that the roadmap reflects the current development status.

Documentation updates are part of the definition of done.

A development task is not complete until implementation, verification, and
documentation have all been addressed.

### 7. Final response

At the end of every task, provide a concise report containing:

1. Changes implemented.
2. Verification performed and actual results.
3. Documentation files updated.
4. Remaining issues.
5. Recommended next step.

If some tests could not be executed, explain why.

If documentation could not be updated, explain what remains incomplete.

## Important

- Do not create Git commits automatically unless explicitly requested.
- Do not modify unrelated project files.
- Do not claim completion before finishing the verification and documentation
  phases.

These instructions remain active for all subsequent Codex development tasks in
this repository.
