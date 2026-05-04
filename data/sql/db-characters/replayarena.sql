CREATE TABLE IF NOT EXISTS `character_arena_replays` (
  `id` INT NOT NULL AUTO_INCREMENT,
  `arenaTypeId` INT NULL DEFAULT NULL,
  `typeId` INT NULL DEFAULT NULL,
  `contentSize` INT NULL DEFAULT NULL,
  `contents` LONGTEXT NULL,
  `mapId` INT NULL DEFAULT NULL,
  `winnerTeamName` VARCHAR(255) NULL DEFAULT NULL,
  `winnerTeamRating` INT NULL DEFAULT NULL,
  `winnerTeamMMR` INT NULL DEFAULT NULL,
  `loserTeamName` VARCHAR(255) NULL DEFAULT NULL,
  `loserTeamRating` INT NULL DEFAULT NULL,
  `loserTeamMMR` INT NULL DEFAULT NULL,
  `winnerPlayerGuids` VARCHAR(255) NULL DEFAULT NULL,
  `loserPlayerGuids` VARCHAR(255) NULL DEFAULT NULL,
  `winnerActorTrack` LONGTEXT NULL,
  `loserActorTrack` LONGTEXT NULL,
  `actorAppearanceSnapshots` LONGTEXT NULL,
  `timesWatched` INT NOT NULL DEFAULT 0,
  `timestamp` TIMESTAMP NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`) USING BTREE
) ENGINE=InnoDB CHARACTER SET=utf8mb4 COLLATE=utf8mb4_unicode_ci ROW_FORMAT=DYNAMIC;

-- Existing installs: keep schema in sync with current module code.
SET @arenaReplayDb := DATABASE();

SET @stmt := IF(
  EXISTS (
    SELECT 1
    FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = @arenaReplayDb
      AND TABLE_NAME = 'character_arena_replays'
      AND COLUMN_NAME = 'winnerActorTrack'
  ),
  'SELECT 1',
  'ALTER TABLE `character_arena_replays` ADD COLUMN `winnerActorTrack` LONGTEXT NULL AFTER `loserPlayerGuids`'
);
PREPARE stmt FROM @stmt;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @stmt := IF(
  EXISTS (
    SELECT 1
    FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = @arenaReplayDb
      AND TABLE_NAME = 'character_arena_replays'
      AND COLUMN_NAME = 'loserActorTrack'
  ),
  'SELECT 1',
  'ALTER TABLE `character_arena_replays` ADD COLUMN `loserActorTrack` LONGTEXT NULL AFTER `winnerActorTrack`'
);
PREPARE stmt FROM @stmt;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;


SET @stmt := IF(
  EXISTS (
    SELECT 1
    FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = @arenaReplayDb
      AND TABLE_NAME = 'character_arena_replays'
      AND COLUMN_NAME = 'actorAppearanceSnapshots'
  ),
  'SELECT 1',
  'ALTER TABLE `character_arena_replays` ADD COLUMN `actorAppearanceSnapshots` LONGTEXT NULL AFTER `loserActorTrack`'
);
PREPARE stmt FROM @stmt;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @stmt := IF(
  EXISTS (
    SELECT 1
    FROM INFORMATION_SCHEMA.STATISTICS
    WHERE TABLE_SCHEMA = @arenaReplayDb
      AND TABLE_NAME = 'character_arena_replays'
      AND INDEX_NAME = 'idx_arena_type_timestamp'
  ),
  'SELECT 1',
  'ALTER TABLE `character_arena_replays` ADD INDEX `idx_arena_type_timestamp` (`arenaTypeId`, `timestamp`)'
);
PREPARE stmt FROM @stmt;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @stmt := IF(
  EXISTS (
    SELECT 1
    FROM INFORMATION_SCHEMA.STATISTICS
    WHERE TABLE_SCHEMA = @arenaReplayDb
      AND TABLE_NAME = 'character_arena_replays'
      AND INDEX_NAME = 'idx_times_watched_rating'
  ),
  'SELECT 1',
  'ALTER TABLE `character_arena_replays` ADD INDEX `idx_times_watched_rating` (`timesWatched`, `winnerTeamRating`)'
);
PREPARE stmt FROM @stmt;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @stmt := IF(
  EXISTS (
    SELECT 1
    FROM INFORMATION_SCHEMA.STATISTICS
    WHERE TABLE_SCHEMA = @arenaReplayDb
      AND TABLE_NAME = 'character_arena_replays'
      AND INDEX_NAME = 'idx_timestamp'
  ),
  'SELECT 1',
  'ALTER TABLE `character_arena_replays` ADD INDEX `idx_timestamp` (`timestamp`)'
);
PREPARE stmt FROM @stmt;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;


CREATE TABLE IF NOT EXISTS `character_arena_replay_actor_snapshot` (
  `id` INT NOT NULL AUTO_INCREMENT,
  `replay_id` INT NOT NULL,
  `actor_guid` BIGINT UNSIGNED NOT NULL,
  `winner_side` TINYINT(1) NOT NULL DEFAULT 0,
  `actor_name` VARCHAR(50) NOT NULL DEFAULT '',
  `player_class` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `race` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `gender` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `display_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `native_display_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `mainhand_display_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `offhand_display_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `ranged_display_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `mainhand_item_entry` INT UNSIGNED NOT NULL DEFAULT 0,
  `offhand_item_entry` INT UNSIGNED NOT NULL DEFAULT 0,
  `ranged_item_entry` INT UNSIGNED NOT NULL DEFAULT 0,
  `skin` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `face` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `hair_style` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `hair_color` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `facial_hair` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `player_bytes` INT UNSIGNED NOT NULL DEFAULT 0,
  `player_bytes_2` INT UNSIGNED NOT NULL DEFAULT 0,
  `player_flags` INT UNSIGNED NOT NULL DEFAULT 0,
  `shapeshift_display_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `shapeshift_form` INT UNSIGNED NOT NULL DEFAULT 0,
  `head_item_entry` INT UNSIGNED NOT NULL DEFAULT 0,
  `shoulders_item_entry` INT UNSIGNED NOT NULL DEFAULT 0,
  `chest_item_entry` INT UNSIGNED NOT NULL DEFAULT 0,
  `waist_item_entry` INT UNSIGNED NOT NULL DEFAULT 0,
  `legs_item_entry` INT UNSIGNED NOT NULL DEFAULT 0,
  `feet_item_entry` INT UNSIGNED NOT NULL DEFAULT 0,
  `wrists_item_entry` INT UNSIGNED NOT NULL DEFAULT 0,
  `hands_item_entry` INT UNSIGNED NOT NULL DEFAULT 0,
  `back_item_entry` INT UNSIGNED NOT NULL DEFAULT 0,
  `tabard_item_entry` INT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`id`) USING BTREE,
  UNIQUE INDEX `unique_replay_actor` (`replay_id`, `actor_guid`) USING BTREE,
  INDEX `idx_replay_id` (`replay_id`) USING BTREE
) ENGINE=InnoDB CHARACTER SET=utf8mb4 COLLATE=utf8mb4_unicode_ci ROW_FORMAT=DYNAMIC;
