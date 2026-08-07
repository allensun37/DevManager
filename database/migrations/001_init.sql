CREATE TABLE repository_state(
    singleton INTEGER PRIMARY KEY CHECK(singleton = 1),
    next_id TEXT NOT NULL CHECK(instr(next_id, char(0)) = 0 AND length(CAST(next_id AS BLOB)) = 20 AND length(next_id)=20 AND next_id NOT GLOB '*[^0-9]*' AND next_id >= '00000000000000000001' AND next_id <= '18446744073709551615')
);

CREATE TABLE projects(
    id TEXT NOT NULL PRIMARY KEY CHECK(instr(id, char(0)) = 0 AND length(CAST(id AS BLOB)) = 20 AND length(id)=20 AND id NOT GLOB '*[^0-9]*' AND id >= '00000000000000000001' AND id <= '18446744073709551614'),
    name TEXT NOT NULL,
    normalized_name TEXT NOT NULL,
    description TEXT NOT NULL,
    status TEXT NOT NULL,
    normalized_status TEXT NOT NULL,
    status_sort_key TEXT NOT NULL
);

CREATE TABLE project_tags(
    project_id TEXT NOT NULL,
    position INTEGER NOT NULL CHECK(typeof(position) = 'integer' AND position >= 0),
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
