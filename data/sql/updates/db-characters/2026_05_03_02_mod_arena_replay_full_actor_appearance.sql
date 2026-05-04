-- Arena Replay full actor appearance capture fields for future player-body replay backends.

SET @arenaReplayDb := DATABASE();

SET @stmt := IF(EXISTS (SELECT 1 FROM INFORMATION_SCHEMA.COLUMNS WHERE TABLE_SCHEMA = @arenaReplayDb AND TABLE_NAME = 'character_arena_replay_actor_snapshot' AND COLUMN_NAME = 'skin'), 'SELECT 1', 'ALTER TABLE `character_arena_replay_actor_snapshot` ADD COLUMN `skin` TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER `ranged_item_entry`');
PREPARE stmt FROM @stmt; EXECUTE stmt; DEALLOCATE PREPARE stmt;

SET @stmt := IF(EXISTS (SELECT 1 FROM INFORMATION_SCHEMA.COLUMNS WHERE TABLE_SCHEMA = @arenaReplayDb AND TABLE_NAME = 'character_arena_replay_actor_snapshot' AND COLUMN_NAME = 'face'), 'SELECT 1', 'ALTER TABLE `character_arena_replay_actor_snapshot` ADD COLUMN `face` TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER `skin`');
PREPARE stmt FROM @stmt; EXECUTE stmt; DEALLOCATE PREPARE stmt;

SET @stmt := IF(EXISTS (SELECT 1 FROM INFORMATION_SCHEMA.COLUMNS WHERE TABLE_SCHEMA = @arenaReplayDb AND TABLE_NAME = 'character_arena_replay_actor_snapshot' AND COLUMN_NAME = 'hair_style'), 'SELECT 1', 'ALTER TABLE `character_arena_replay_actor_snapshot` ADD COLUMN `hair_style` TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER `face`');
PREPARE stmt FROM @stmt; EXECUTE stmt; DEALLOCATE PREPARE stmt;

SET @stmt := IF(EXISTS (SELECT 1 FROM INFORMATION_SCHEMA.COLUMNS WHERE TABLE_SCHEMA = @arenaReplayDb AND TABLE_NAME = 'character_arena_replay_actor_snapshot' AND COLUMN_NAME = 'hair_color'), 'SELECT 1', 'ALTER TABLE `character_arena_replay_actor_snapshot` ADD COLUMN `hair_color` TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER `hair_style`');
PREPARE stmt FROM @stmt; EXECUTE stmt; DEALLOCATE PREPARE stmt;

SET @stmt := IF(EXISTS (SELECT 1 FROM INFORMATION_SCHEMA.COLUMNS WHERE TABLE_SCHEMA = @arenaReplayDb AND TABLE_NAME = 'character_arena_replay_actor_snapshot' AND COLUMN_NAME = 'facial_hair'), 'SELECT 1', 'ALTER TABLE `character_arena_replay_actor_snapshot` ADD COLUMN `facial_hair` TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER `hair_color`');
PREPARE stmt FROM @stmt; EXECUTE stmt; DEALLOCATE PREPARE stmt;

SET @stmt := IF(EXISTS (SELECT 1 FROM INFORMATION_SCHEMA.COLUMNS WHERE TABLE_SCHEMA = @arenaReplayDb AND TABLE_NAME = 'character_arena_replay_actor_snapshot' AND COLUMN_NAME = 'player_bytes'), 'SELECT 1', 'ALTER TABLE `character_arena_replay_actor_snapshot` ADD COLUMN `player_bytes` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `facial_hair`');
PREPARE stmt FROM @stmt; EXECUTE stmt; DEALLOCATE PREPARE stmt;

SET @stmt := IF(EXISTS (SELECT 1 FROM INFORMATION_SCHEMA.COLUMNS WHERE TABLE_SCHEMA = @arenaReplayDb AND TABLE_NAME = 'character_arena_replay_actor_snapshot' AND COLUMN_NAME = 'player_bytes_2'), 'SELECT 1', 'ALTER TABLE `character_arena_replay_actor_snapshot` ADD COLUMN `player_bytes_2` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `player_bytes`');
PREPARE stmt FROM @stmt; EXECUTE stmt; DEALLOCATE PREPARE stmt;

SET @stmt := IF(EXISTS (SELECT 1 FROM INFORMATION_SCHEMA.COLUMNS WHERE TABLE_SCHEMA = @arenaReplayDb AND TABLE_NAME = 'character_arena_replay_actor_snapshot' AND COLUMN_NAME = 'player_flags'), 'SELECT 1', 'ALTER TABLE `character_arena_replay_actor_snapshot` ADD COLUMN `player_flags` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `player_bytes_2`');
PREPARE stmt FROM @stmt; EXECUTE stmt; DEALLOCATE PREPARE stmt;

SET @stmt := IF(EXISTS (SELECT 1 FROM INFORMATION_SCHEMA.COLUMNS WHERE TABLE_SCHEMA = @arenaReplayDb AND TABLE_NAME = 'character_arena_replay_actor_snapshot' AND COLUMN_NAME = 'shapeshift_display_id'), 'SELECT 1', 'ALTER TABLE `character_arena_replay_actor_snapshot` ADD COLUMN `shapeshift_display_id` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `player_flags`');
PREPARE stmt FROM @stmt; EXECUTE stmt; DEALLOCATE PREPARE stmt;

SET @stmt := IF(EXISTS (SELECT 1 FROM INFORMATION_SCHEMA.COLUMNS WHERE TABLE_SCHEMA = @arenaReplayDb AND TABLE_NAME = 'character_arena_replay_actor_snapshot' AND COLUMN_NAME = 'shapeshift_form'), 'SELECT 1', 'ALTER TABLE `character_arena_replay_actor_snapshot` ADD COLUMN `shapeshift_form` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `shapeshift_display_id`');
PREPARE stmt FROM @stmt; EXECUTE stmt; DEALLOCATE PREPARE stmt;

SET @stmt := IF(EXISTS (SELECT 1 FROM INFORMATION_SCHEMA.COLUMNS WHERE TABLE_SCHEMA = @arenaReplayDb AND TABLE_NAME = 'character_arena_replay_actor_snapshot' AND COLUMN_NAME = 'head_item_entry'), 'SELECT 1', 'ALTER TABLE `character_arena_replay_actor_snapshot` ADD COLUMN `head_item_entry` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `shapeshift_form`');
PREPARE stmt FROM @stmt; EXECUTE stmt; DEALLOCATE PREPARE stmt;

SET @stmt := IF(EXISTS (SELECT 1 FROM INFORMATION_SCHEMA.COLUMNS WHERE TABLE_SCHEMA = @arenaReplayDb AND TABLE_NAME = 'character_arena_replay_actor_snapshot' AND COLUMN_NAME = 'shoulders_item_entry'), 'SELECT 1', 'ALTER TABLE `character_arena_replay_actor_snapshot` ADD COLUMN `shoulders_item_entry` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `head_item_entry`');
PREPARE stmt FROM @stmt; EXECUTE stmt; DEALLOCATE PREPARE stmt;

SET @stmt := IF(EXISTS (SELECT 1 FROM INFORMATION_SCHEMA.COLUMNS WHERE TABLE_SCHEMA = @arenaReplayDb AND TABLE_NAME = 'character_arena_replay_actor_snapshot' AND COLUMN_NAME = 'chest_item_entry'), 'SELECT 1', 'ALTER TABLE `character_arena_replay_actor_snapshot` ADD COLUMN `chest_item_entry` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `shoulders_item_entry`');
PREPARE stmt FROM @stmt; EXECUTE stmt; DEALLOCATE PREPARE stmt;

SET @stmt := IF(EXISTS (SELECT 1 FROM INFORMATION_SCHEMA.COLUMNS WHERE TABLE_SCHEMA = @arenaReplayDb AND TABLE_NAME = 'character_arena_replay_actor_snapshot' AND COLUMN_NAME = 'waist_item_entry'), 'SELECT 1', 'ALTER TABLE `character_arena_replay_actor_snapshot` ADD COLUMN `waist_item_entry` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `chest_item_entry`');
PREPARE stmt FROM @stmt; EXECUTE stmt; DEALLOCATE PREPARE stmt;

SET @stmt := IF(EXISTS (SELECT 1 FROM INFORMATION_SCHEMA.COLUMNS WHERE TABLE_SCHEMA = @arenaReplayDb AND TABLE_NAME = 'character_arena_replay_actor_snapshot' AND COLUMN_NAME = 'legs_item_entry'), 'SELECT 1', 'ALTER TABLE `character_arena_replay_actor_snapshot` ADD COLUMN `legs_item_entry` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `waist_item_entry`');
PREPARE stmt FROM @stmt; EXECUTE stmt; DEALLOCATE PREPARE stmt;

SET @stmt := IF(EXISTS (SELECT 1 FROM INFORMATION_SCHEMA.COLUMNS WHERE TABLE_SCHEMA = @arenaReplayDb AND TABLE_NAME = 'character_arena_replay_actor_snapshot' AND COLUMN_NAME = 'feet_item_entry'), 'SELECT 1', 'ALTER TABLE `character_arena_replay_actor_snapshot` ADD COLUMN `feet_item_entry` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `legs_item_entry`');
PREPARE stmt FROM @stmt; EXECUTE stmt; DEALLOCATE PREPARE stmt;

SET @stmt := IF(EXISTS (SELECT 1 FROM INFORMATION_SCHEMA.COLUMNS WHERE TABLE_SCHEMA = @arenaReplayDb AND TABLE_NAME = 'character_arena_replay_actor_snapshot' AND COLUMN_NAME = 'wrists_item_entry'), 'SELECT 1', 'ALTER TABLE `character_arena_replay_actor_snapshot` ADD COLUMN `wrists_item_entry` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `feet_item_entry`');
PREPARE stmt FROM @stmt; EXECUTE stmt; DEALLOCATE PREPARE stmt;

SET @stmt := IF(EXISTS (SELECT 1 FROM INFORMATION_SCHEMA.COLUMNS WHERE TABLE_SCHEMA = @arenaReplayDb AND TABLE_NAME = 'character_arena_replay_actor_snapshot' AND COLUMN_NAME = 'hands_item_entry'), 'SELECT 1', 'ALTER TABLE `character_arena_replay_actor_snapshot` ADD COLUMN `hands_item_entry` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `wrists_item_entry`');
PREPARE stmt FROM @stmt; EXECUTE stmt; DEALLOCATE PREPARE stmt;

SET @stmt := IF(EXISTS (SELECT 1 FROM INFORMATION_SCHEMA.COLUMNS WHERE TABLE_SCHEMA = @arenaReplayDb AND TABLE_NAME = 'character_arena_replay_actor_snapshot' AND COLUMN_NAME = 'back_item_entry'), 'SELECT 1', 'ALTER TABLE `character_arena_replay_actor_snapshot` ADD COLUMN `back_item_entry` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `hands_item_entry`');
PREPARE stmt FROM @stmt; EXECUTE stmt; DEALLOCATE PREPARE stmt;

SET @stmt := IF(EXISTS (SELECT 1 FROM INFORMATION_SCHEMA.COLUMNS WHERE TABLE_SCHEMA = @arenaReplayDb AND TABLE_NAME = 'character_arena_replay_actor_snapshot' AND COLUMN_NAME = 'tabard_item_entry'), 'SELECT 1', 'ALTER TABLE `character_arena_replay_actor_snapshot` ADD COLUMN `tabard_item_entry` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `back_item_entry`');
PREPARE stmt FROM @stmt; EXECUTE stmt; DEALLOCATE PREPARE stmt;
