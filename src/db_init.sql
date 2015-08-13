-- users
CREATE TABLE IF NOT EXISTS `users` (
	`id` INTEGER NOT NULL,
	`username` VARCHAR(30) NOT NULL,
	`first_name` VARCHAR(60) NOT NULL,
	`last_name` VARCHAR(60) NOT NULL,
	`email` VARCHAR(60) NOT NULL,
	`password` CHAR(64) NOT NULL,
	`type` TINYINT(1) NOT NULL DEFAULT 2,
	PRIMARY KEY (`id`),
	UNIQUE (`username`)
);
INSERT OR IGNORE INTO users VALUES (1, 'sim' , '', '', '', '507a9a8be3d145a86daa9644b28bf42a8dc0720d8baeabdf0406c393692bf082', 0);

-- session
CREATE TABLE IF NOT EXISTS `session` (
	`id` CHAR(10) NOT NULL,
	`user_id` INTEGER NOT NULL,
	`data` TEXT NOT NULL,
	`ip` CHAR(15) NOT NULL,
	`user_agent` TEXT NOT NULL,
	`time` DATETIME NOT NULL,
	PRIMARY KEY (`id`)
);
CREATE INDEX IF NOT EXISTS session_1 ON session(user_id);
CREATE INDEX IF NOT EXISTS session_2 ON session(time);

-- problems
CREATE TABLE IF NOT EXISTS `problems` (
	`id` INTEGER NOT NULL,
	`public_access` BOOLEAN NOT NULL DEFAULT FALSE,
	`name` VARCHAR(128) NOT NULL,
	`owner` INTEGER NOT NULL,
	`added` DATETIME NOT NULL,
	PRIMARY KEY (`id`)
);
CREATE INDEX IF NOT EXISTS problems_1 ON problems(owner);

-- rounds
CREATE TABLE IF NOT EXISTS `rounds` (
	`id` INTEGER NOT NULL,
	`parent` INTEGER NULL DEFAULT NULL,
	`grandparent` INTEGER NULL DEFAULT NULL,
	`problem_id` INTEGER DEFAULT NULL,
	`public_access` BOOLEAN NOT NULL DEFAULT FALSE,
	`name` VARCHAR(128) NOT NULL,
	`owner` INTEGER NOT NULL,
	`item` INTEGER NOT NULL,
	`visible` BOOLEAN NOT NULL DEFAULT FALSE,
	`begins` DATETIME NULL DEFAULT NULL,
	`full_results` DATETIME NULL DEFAULT NULL,
	`ends` DATETIME NULL DEFAULT NULL,
	PRIMARY KEY (`id`)
);
CREATE INDEX IF NOT EXISTS rounds_1 ON rounds(parent, visible);
CREATE INDEX IF NOT EXISTS rounds_2 ON rounds(parent, begins);
CREATE INDEX IF NOT EXISTS rounds_3 ON rounds(parent, public_access);
CREATE INDEX IF NOT EXISTS rounds_4 ON rounds(grandparent, item);
CREATE INDEX IF NOT EXISTS rounds_5 ON rounds(owner);

-- users_to_contests
CREATE TABLE IF NOT EXISTS `users_to_contests` (
	`user_id` INTEGER NOT NULL,
	`contest_id` INTEGER NOT NULL,
	PRIMARY KEY (`user_id`, `contest_id`)
);
CREATE INDEX IF NOT EXISTS users_to_contests_1 ON users_to_contests(contest_id);

-- submissions
CREATE TABLE IF NOT EXISTS `submissions` (
	`id` INTEGER NOT NULL,
	`user_id` INTEGER NOT NULL,
	`problem_id` INTEGER NOT NULL,
	`round_id` INTEGER NOT NULL,
	`parent_round_id` INTEGER NOT NULL,
	`contest_round_id` INTEGER NOT NULL,
	`final` BOOLEAN NOT NULL DEFAULT FALSE,
	`submit_time` DATETIME NOT NULL,
	`status` INTEGER NOT NULL,
	`score` INTEGER NULL DEFAULT NULL,
	`queued` DATETIME NOT NULL,
	PRIMARY KEY (`id`)
);
CREATE INDEX IF NOT EXISTS submissions_1 ON submissions(status, queued);
CREATE INDEX IF NOT EXISTS submissions_2 ON submissions(user_id, round_id, final);

-- submissions_to_rounds
CREATE TABLE IF NOT EXISTS `submissions_to_rounds` (
	`submission_id` INTEGER NOT NULL,
	`round_id` INTEGER NOT NULL,
	`user_id` INTEGER NOT NULL,
	`submit_time` DATETIME NOT NULL,
	`final` BOOLEAN NOT NULL DEFAULT FALSE,
	PRIMARY KEY (`round_id`, `submission_id`)
);
CREATE INDEX IF NOT EXISTS submissions_to_rounds_1 ON submissions_to_rounds(submission_id);
CREATE INDEX IF NOT EXISTS submissions_to_rounds_2 ON submissions_to_rounds(round_id, user_id, submit_time);
CREATE INDEX IF NOT EXISTS submissions_to_rounds_3 ON submissions_to_rounds(round_id, user_id, final, submit_time);
CREATE INDEX IF NOT EXISTS submissions_to_rounds_4 ON submissions_to_rounds(round_id, submit_time);
CREATE INDEX IF NOT EXISTS submissions_to_rounds_5 ON submissions_to_rounds(round_id, final, submit_time);
