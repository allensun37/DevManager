# DevManager CLI Menu Design

**Goal:** Provide the v0.1 terminal interface for the completed project management and JSON persistence layers.

## Architecture

`main` is the composition root. It creates `JsonProjectRepository` with the fixed path `data/projects.json`, passes it to `ProjectManager`, and starts `MenuController`.

`MenuController` owns the loop, prompts, parsing and terminal output only. It receives `std::istream` and `std::ostream` by reference, so scripted input/output tests exercise the same interaction behavior as the executable. It calls `ProjectManager` for all operations and never reads or writes JSON itself.

## Menu

The loop displays these commands on every iteration:

1. List projects
2. Add a project
3. Delete a project
4. Search by name
5. Search by technology
0. Exit

An unrecognised command prints a short error and returns to the menu. Exit prints a farewell message and returns from `run()`.

## Input and Output Rules

- All input is line based via `std::getline`; the implementation must not leave a failed formatted-input state behind.
- Adding collects name, comma-separated technology tags, optional description, and status. Blank name, blank status, no tags, or blank tags report the `Project` validation error and return to the menu without saving a project.
- Listing and search results show `ID`, name, technology tags, description and status. An empty result prints an explicit message.
- Deleting asks for an ID. Non-numeric, zero and unknown IDs report a clear message and return to the menu. For a known ID, the controller asks for `y/n`; only `y` performs deletion.
- Name and technology queries are passed to `ProjectManager`; blank queries simply produce its empty result.
- Repository load/write errors are caught by `main`, printed to `std::cerr`, and cause a non-zero process exit. The menu does not silently continue with a replacement data store.

## Testing

`MenuControllerTests` uses `std::istringstream` and `std::ostringstream` with a default in-memory `ProjectManager` to test list/add/delete/search, invalid command and malformed ID recovery, and delete cancellation. A smoke-style executable test verifies that `0` exits cleanly.

## Scope

This stage adds no edit command, no HTTP/Web UI, no database implementation and no changes to `Project` validation or JSON format.
