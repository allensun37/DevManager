CREATE TABLE repository_state(
    singleton INTEGER PRIMARY KEY CHECK(singleton = 1),
    next_id TEXT NOT NULL CHECK(length(next_id)=20 AND next_id NOT GLOB '*[^0-9]*' AND next_id >= '00000000000000000001' AND next_id <= '18446744073709551615')
);

CREATE TABLE projects(
    id TEXT PRIMARY KEY CHECK(length(id)=20 AND id NOT GLOB '*[^0-9]*' AND id >= '00000000000000000001' AND id <= '18446744073709551614'),
    name TEXT NOT NULL,
    normalized_name TEXT NOT NULL,
    description TEXT NOT NULL,
    status TEXT NOT NULL,
    normalized_status TEXT NOT NULL,
    status_sort_key TEXT NOT NULL
);

CREATE TABLE project_tags(
    project_id TEXT NOT NULL,
    position INTEGER NOT NULL CHECK(position >= 0),
    tag TEXT NOT NULL,
    normalized_tag TEXT NOT NULL,
    PRIMARY KEY(project_id, position),
    FOREIGN KEY(project_id) REFERENCES projects(id) ON DELETE CASCADE
);

CREATE INDEX idx_projects_normalized_status ON projects(normalized_status);
CREATE INDEX idx_project_tags_normalized_tag ON project_tags(normalized_tag);
CREATE INDEX idx_project_tags_project_id ON project_tags(project_id);

INSERT INTO repository_state(singleton, next_id)
VALUES (1, '00000000000000000001');
