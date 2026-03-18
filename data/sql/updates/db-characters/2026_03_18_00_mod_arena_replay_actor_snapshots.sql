-- Arena Replay actor appearance snapshots for clone-scene playback.

SET @arenaReplayDb := DATABASE();

SET @stmt := IF(
  EXISTS (
    SELECT 1
    FROM INFORMATION_SCHEMA.TABLES
    WHERE TABLE_SCHEMA = @arenaReplayDb
      AND TABLE_NAME = 'character_arena_replay_actor_snapshot'
  ),
  'SELECT 1',
  'CREATE TABLE `character_arena_replay_actor_snapshot` (`id` INT NOT NULL AUTO_INCREMENT, `replay_id` INT NOT NULL, `actor_guid` BIGINT UNSIGNED NOT NULL, `winner_side` TINYINT(1) NOT NULL DEFAULT 0, `actor_name` VARCHAR(50) NOT NULL DEFAULT "", `player_class` TINYINT UNSIGNED NOT NULL DEFAULT 0, `race` TINYINT UNSIGNED NOT NULL DEFAULT 0, `gender` TINYINT UNSIGNED NOT NULL DEFAULT 0, `display_id` INT UNSIGNED NOT NULL DEFAULT 0, `native_display_id` INT UNSIGNED NOT NULL DEFAULT 0, `mainhand_display_id` INT UNSIGNED NOT NULL DEFAULT 0, `offhand_display_id` INT UNSIGNED NOT NULL DEFAULT 0, `ranged_display_id` INT UNSIGNED NOT NULL DEFAULT 0, PRIMARY KEY (`id`) USING BTREE, UNIQUE INDEX `unique_replay_actor` (`replay_id`, `actor_guid`) USING BTREE, INDEX `idx_replay_id` (`replay_id`) USING BTREE) ENGINE=InnoDB CHARACTER SET=utf8mb4 COLLATE=utf8mb4_unicode_ci ROW_FORMAT=DYNAMIC'
);
PREPARE stmt FROM @stmt;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;
